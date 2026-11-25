// AutoIME user-phrase manager — thin ACE AV launcher (PLAN-072 T-01).
//
// What this binary is (and is not)
// --------------------------------
// The manager's behaviour is plain Auto source under `manager/src/`; this
// native entry only:
//   1. discovers that source closure next to itself (`<exe_dir>\manager\src`)
//      or, for development, at `AUTOIME_MANAGER_DIR`;
//   2. assembles it with the frozen ACE system library (`std.file/path/env/
//      process/text/conv`) and compiles it through the shared ACE front door;
//   3. runs the compiled product on the new ACE AV (never the legacy AutoVM),
//      with the process command line as the program's argv and a per-run
//      capability grant;
//   4. re-emits the program's captured console streams byte-exactly and exits
//      with the program's own status.
//
// Named upstream defect (T-01 finding, auto-lang owner): HEAD's frozen 972
// assembled set cannot parse `stdlib/ace/asm/list.at` — the module uses the
// index expression `xs[i]`, which the packages-modules front door rejects
// ("`fn` is not part of this source profile"); the repo's own
// `sys_d01_d06_1064::v02_d01_d02_file_text_range_and_dir_names_av` fails for
// the same reason. The manager's import closure never names `std.list`, so
// this launcher removes exactly that one frozen module from the assembled
// input set and keeps the AV lane. When auto-lang lands the parse fix this
// filter becomes a no-op and must be re-verified, not silently kept.
//
// Requirements are recorded in docs/plans/072-autoime-ace-phrase-manager.md
// (§9, T-01) and manager/README.md.

use std::path::{Path, PathBuf};
use std::time::Instant;

use ace_av::modules::packages::PackageAvTarget;
use ace_av_core::contract::ExecOutcome;
use ace_av_core::host::{ConsoleHost, HostError, Stream};
use ace_av_core::platform_io::Win32PlatformIo;
use ace_av_core::{CancelFlag, ExecLimits};
use ace_core::source::cst::FrontBudget;
use ace_core::source::packages::{
    AssemblyBackend, PackageAssemblyContext, ProviderChoice, TargetOs,
};
use ace_source_query::modules::packages::{
    assemble_stdlib_sys_inputs, compile_assembled_aggregates_inputs,
};

/// Bounded console capture (the AV console lane's per-run cap is the same
/// 64 KiB budget the system library uses for piped output).
#[derive(Default)]
struct Passthrough {
    stdout: Vec<u8>,
    stderr: Vec<u8>,
}

impl ConsoleHost for Passthrough {
    fn retained_bytes(&self) -> usize {
        self.stdout.len() + self.stderr.len()
    }
    fn write(
        &mut self,
        stream: Stream,
        bytes: &[u8],
        _cancel: &CancelFlag,
    ) -> Result<(), HostError> {
        if bytes.len() > ace_av_core::host::MAX_CALL_BYTES
            || self.retained_bytes() + bytes.len() > ace_av_core::host::MAX_RUN_BYTES
        {
            return Err(HostError::Resource);
        }
        match stream {
            Stream::Stdout => self.stdout.extend_from_slice(bytes),
            Stream::Stderr => self.stderr.extend_from_slice(bytes),
        }
        Ok(())
    }
}

/// The exact capabilities the manager's Auto source declares. Nothing wider:
/// no network, no env writes, no spawn beyond the documented deploy child.
fn grants() -> Vec<String> {
    vec![
        "console.stdout".into(),
        "console.stderr".into(),
        "platform.io.stdout".into(),
        "platform.io.stderr".into(),
        "platform.io.stdin".into(),
        "platform.io.write".into(),
        "platform.mem".into(),
        "platform.file".into(),
        "platform.file.path".into(),
        "platform.env".into(),
        "platform.clock".into(),
        "platform.entropy".into(),
        "platform.process".into(),
    ]
}

fn manager_dir() -> PathBuf {
    if let Some(v) = std::env::var_os("AUTOIME_MANAGER_DIR") {
        return PathBuf::from(v);
    }
    let exe = std::env::current_exe().unwrap_or_default();
    let exe_dir = exe.parent().unwrap_or(Path::new(".")).to_path_buf();
    // Installed layout: <install>\\autoime-config.exe + <install>\\manager\\src.
    // Dev build: <repo>\\manager\\bin\\autoime-config.exe + <repo>\\manager\\src.
    let candidates = [
        exe_dir.join("manager"),
        exe_dir
            .parent()
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| exe_dir.clone()),
        exe_dir.clone(),
    ];
    for c in candidates {
        if c.join("src").is_dir() || c.join("main.at").is_file() {
            return c;
        }
    }
    exe_dir.join("manager")
}

/// The ACE managed store is per-run build scratch (it holds the assembled
/// product's pages and grows with every generation), not user data: a fresh
/// temporary store keeps the launcher's cost flat and leaves nothing behind.
/// `AUTOIME_MANAGER_STORE` pins a directory for repeated measurements.
fn store_dir() -> (PathBuf, bool) {
    if let Some(v) = std::env::var_os("AUTOIME_MANAGER_STORE") {
        return (PathBuf::from(v), false);
    }
    let unique = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    let dir = std::env::temp_dir().join(format!(
        "autoime-manager-store-{}-{unique}",
        std::process::id()
    ));
    (dir, true)
}

fn fail(code: i32, msg: String) -> ! {
    eprintln!("autoime-config: {msg}");
    std::process::exit(code);
}

fn main() {
    let started = Instant::now();
    let trace = std::env::var_os("AUTOIME_MANAGER_TRACE").is_some();

    // The Auto program resolves sibling tools (autoime_deploy.exe) next to the
    // installed launcher; expose that directory without inventing argv flags.
    if std::env::var_os("AUTOIME_INSTALL_DIR").is_none() {
        if let Ok(exe) = std::env::current_exe() {
            if let Some(dir) = exe.parent() {
                std::env::set_var("AUTOIME_INSTALL_DIR", dir);
            }
        }
    }
    let root = manager_dir();
    let src_dir = if root.join("src").is_dir() {
        root.join("src")
    } else {
        root.clone()
    };
    let mut app_files: Vec<(String, String)> = Vec::new();
    let entries = match std::fs::read_dir(&src_dir) {
        Ok(v) => v,
        Err(e) => fail(
            70,
            format!(
                "manager sources not found at {} ({e}); set AUTOIME_MANAGER_DIR for a dev run",
                src_dir.display()
            ),
        ),
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|e| e.to_str()) != Some("at") {
            continue;
        }
        let name = path
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or_default()
            .to_string();
        let Ok(src) = std::fs::read_to_string(&path) else {
            fail(70, format!("cannot read {}", path.display()));
        };
        app_files.push((name, src));
    }
    app_files.sort_by(|a, b| a.0.cmp(&b.0));
    if !app_files.iter().any(|(n, _)| n == "main.at") {
        fail(70, format!("no main.at under {}", src_dir.display()));
    }

    let t_assemble = Instant::now();
    let mut ctx = PackageAssemblyContext::new(
        AssemblyBackend::Av,
        ProviderChoice::Auto,
        TargetOs::Windows,
    );
    let mut inputs = match assemble_stdlib_sys_inputs(&app_files, "app::main", &mut ctx) {
        Ok(v) => v,
        Err(e) => fail(71, format!("assembly: {e}")),
    };
    // The manager's import closure (std.file/path/env/process/text/bytes/conv)
    // plus their frozen dependencies. The assembled set ships every stdlib
    // module; compiling only the closure keeps the CLI's per-run front-end cost
    // proportional to what the manager actually uses. `std.list` is excluded
    // separately below (named upstream parse defect).
    const NEEDED: &[&str] = &[
        "bytes", "conv", "env", "path", "text", "syscommon", "sysplat_win", "file", "process", "time",
    ];
    inputs.retain(|i| {
        if i.package != "std" {
            return true;
        }
        NEEDED.contains(&i.id.as_str())
    });
    // Named upstream defect (auto-lang owner): HEAD's frozen 972 asm set cannot
    // parse stdlib/ace/asm/list.at (`xs[i]` index expression is rejected). The
    // manager never imports std.list, so the module stays out of the closure.
    inputs.retain(|i| !(i.package == "std" && i.id == "list"));

    let assemble_ms = t_assemble.elapsed().as_millis();
    let t_compile = Instant::now();
    let product = match compile_assembled_aggregates_inputs(
        &inputs,
        "app::main",
        FrontBudget::WORK_OWNED_64MIB,
        &ctx,
    ) {
        Ok(p) => p,
        Err(diags) => fail(
            72,
            format!(
                "compile: {}",
                diags
                    .iter()
                    .map(|d| format!(
                        "[{}] {}@{:?} @{}..{}: {}",
                        d.module,
                        d.diagnostic.code,
                        d.diagnostic.stage,
                        d.diagnostic.span.start,
                        d.diagnostic.span.end,
                        d.diagnostic.message
                    ))
                    .collect::<Vec<_>>()
                    .join("; ")
            ),
        ),
    };

    let compile_ms = t_compile.elapsed().as_millis();
    let t_link = Instant::now();
    let (store, temp_store) = store_dir();
    if let Err(e) = std::fs::create_dir_all(&store) {
        fail(73, format!("store dir {}: {e}", store.display()));
    }
    let target = match PackageAvTarget::cold(&store, &product) {
        Ok(t) => t,
        Err(e) => {
            let _ = std::fs::remove_dir_all(&store);
            fail(73, format!("link: {e}"))
        }
    };

    let link_ms = t_link.elapsed().as_millis();
    let mut console = Passthrough::default();
    let mut backend = Win32PlatformIo::default();
    // The manager is a short-lived CLI; the deploy child may legitimately run
    // for minutes (first cold deploy compiles dictionaries), so the wall budget
    // is the CLI own generous bound rather than the executor default.
    let limits = ExecLimits {
        max_steps: 50_000_000,
        wall: std::time::Duration::from_secs(180),
        ..Default::default()
    };
    let cancel = CancelFlag::default();
    let run_started = Instant::now();
    let result = target.run_platform_value_with(
        &limits,
        &cancel,
        Some(&mut console),
        &grants(),
        &mut backend,
    );
    let run_ms = run_started.elapsed().as_millis();

    use std::io::Write as _;
    let _ = std::io::stdout().write_all(&console.stdout);
    let _ = std::io::stderr().write_all(&console.stderr);
    let _ = std::io::stdout().flush();

    if trace {
        eprintln!(
            "autoime-config: assemble_ms={} compile_ms={} link_ms={} run_ms={} total_ms={}",
            assemble_ms, compile_ms, link_ms, run_ms, started.elapsed().as_millis()
        );
    }

    let code = match result {
        Ok((ExecOutcome::Exit { code }, _)) => code,
        Ok((ExecOutcome::Success { value, .. }, _)) => match value {
            ace_av_core::contract::Value::I32(v) => v,
            _ => 0,
        },
        Ok((other, summary)) => fail(74, format!("outcome={other:?} summary={summary:?}")),
        Err(e) => fail(75, format!("exec: {e}")),
    };
    // The product is fully materialised in memory; release its store scratch.
    drop(target);
    if temp_store {
        let _ = std::fs::remove_dir_all(&store);
    }
    std::process::exit(code);
}
