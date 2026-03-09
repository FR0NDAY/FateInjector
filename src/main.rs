#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use eframe::egui;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

const APP_TITLE: &str = "Fate Client Injector";
const DEFAULT_PROCESS_NAME: &str = "minecraft.windows.exe";
const DEFAULT_DLL_HINT: &str = "Click Browse to select the DLL file";

struct FateInjectorApp {
    working_dir: PathBuf,
    config_path: PathBuf,
    process_name: String,
    custom_target: bool,
    auto_inject: bool,
    delay_seconds: u32,
    dll_path: String,
    status: String,
    last_auto_injected_pid: u32,
    last_auto_attempt: Instant,
}

impl FateInjectorApp {
    fn new(_cc: &eframe::CreationContext<'_>) -> Self {
        let working_dir = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
        let config_path = working_dir.join("config.txt");

        let mut app = Self {
            working_dir,
            config_path,
            process_name: DEFAULT_PROCESS_NAME.to_owned(),
            custom_target: false,
            auto_inject: false,
            delay_seconds: 5,
            dll_path: DEFAULT_DLL_HINT.to_owned(),
            status: "Ready.".to_owned(),
            last_auto_injected_pid: 0,
            last_auto_attempt: Instant::now(),
        };

        if let Ok(cfg) = load_config(&app.config_path) {
            app.custom_target = cfg.custom_proc_name;
            app.delay_seconds = cfg.delay_seconds.clamp(1, 3600);
            app.process_name = if cfg.proc_name.trim().is_empty() {
                DEFAULT_PROCESS_NAME.to_owned()
            } else {
                cfg.proc_name
            };
            app.dll_path = if cfg.dll_path.trim().is_empty() {
                DEFAULT_DLL_HINT.to_owned()
            } else {
                cfg.dll_path
            };
        } else {
            app.save_config();
        }

        if !app.custom_target {
            app.process_name = DEFAULT_PROCESS_NAME.to_owned();
        }

        app
    }

    fn save_config(&mut self) {
        let cfg = PersistedConfig {
            custom_proc_name: self.custom_target,
            delay_seconds: self.delay_seconds.clamp(1, 3600),
            dll_path: self.dll_path.clone(),
            proc_name: self.process_name.clone(),
        };

        if let Err(err) = save_config(&self.config_path, &cfg) {
            self.status = format!("Failed to save config: {err}");
        }
    }

    fn select_dll(&mut self) {
        if let Some(path) = rfd::FileDialog::new()
            .set_directory(&self.working_dir)
            .add_filter("Dynamic Link Library", &["dll"])
            .pick_file()
        {
            self.dll_path = path.to_string_lossy().to_string();
            self.save_config();
        }
    }

    fn auto_tick(&mut self) {
        if !self.auto_inject {
            return;
        }

        let delay = self.delay_seconds.clamp(1, 3600);
        if self.last_auto_attempt.elapsed() < Duration::from_secs(delay as u64) {
            return;
        }

        self.last_auto_attempt = Instant::now();
        let success = self.inject_current_target(true);
        if !success {
            let pid = injector::get_proc_id(self.process_name.trim());
            if pid != 0 {
                self.auto_inject = false;
                self.status.push_str(" | AutoInject disabled");
                self.save_config();
            }
        }
    }

    fn inject_current_target(&mut self, auto_mode: bool) -> bool {
        let prefix = if auto_mode { "AutoInject: " } else { "" };

        let process_name = self.process_name.trim();
        if process_name.is_empty() {
            self.status = format!("{prefix}Process name is empty.");
            return false;
        }

        let proc_id = injector::get_proc_id(process_name);
        if proc_id == 0 {
            self.status = format!("{prefix}Can't find process! | 0");
            return false;
        }

        if auto_mode && proc_id == self.last_auto_injected_pid {
            self.status = format!("{prefix}Already injected! | {proc_id}");
            return true;
        }

        if self.dll_path.trim().is_empty() || self.dll_path == DEFAULT_DLL_HINT {
            self.status = format!("{prefix}Please select a DLL file first.");
            return false;
        }

        let dll_path = PathBuf::from(self.dll_path.trim());

        match injector::run_preflight(proc_id, &dll_path) {
            Ok(()) => {}
            Err(err) => {
                self.status = format!("{prefix}{err}");
                return false;
            }
        }

        match injector::grant_uwp_read_execute(&dll_path) {
            Ok(()) => {}
            Err(err) => {
                self.status = format!("{prefix}{err}");
                return false;
            }
        }

        match injector::perform_injection(proc_id, &dll_path) {
            Ok(()) => {
                self.status = format!("{prefix}Injected successfully into process {proc_id}");
                self.last_auto_injected_pid = proc_id;
                self.save_config();
                true
            }
            Err(err) => {
                self.status = format!("{prefix}{err}");
                false
            }
        }
    }
}

impl eframe::App for FateInjectorApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.auto_tick();

        if self.auto_inject {
            ctx.request_repaint_after(Duration::from_millis(200));
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading(APP_TITLE);
            ui.label("Rust edition");
            ui.separator();

            let mut changed = false;

            ui.group(|ui| {
                ui.label("Target Process");
                ui.horizontal(|ui| {
                    ui.label("Process:");
                    let proc_changed = ui
                        .add_enabled(
                            self.custom_target && !self.auto_inject,
                            egui::TextEdit::singleline(&mut self.process_name).desired_width(290.0),
                        )
                        .changed();
                    changed |= proc_changed;

                    let previous_custom = self.custom_target;
                    let custom_changed = ui
                        .add_enabled(
                            !self.auto_inject,
                            egui::Checkbox::new(&mut self.custom_target, "Custom target"),
                        )
                        .changed();
                    if custom_changed {
                        changed = true;
                        if previous_custom && !self.custom_target {
                            self.process_name = DEFAULT_PROCESS_NAME.to_owned();
                        }
                    }
                });
            });

            ui.add_space(8.0);

            ui.group(|ui| {
                ui.label("Injection");
                ui.horizontal(|ui| {
                    ui.label("DLL:");
                    let dll_changed = ui
                        .add_enabled(
                            !self.auto_inject,
                            egui::TextEdit::singleline(&mut self.dll_path).desired_width(300.0),
                        )
                        .changed();
                    changed |= dll_changed;

                    if ui
                        .add_enabled(!self.auto_inject, egui::Button::new("Browse"))
                        .clicked()
                    {
                        self.select_dll();
                        changed = true;
                    }
                });

                ui.horizontal(|ui| {
                    let previous_auto = self.auto_inject;
                    let auto_changed = ui
                        .checkbox(&mut self.auto_inject, "Auto inject")
                        .changed();
                    if auto_changed {
                        changed = true;
                        if self.auto_inject {
                            self.last_auto_attempt = Instant::now();
                            self.last_auto_injected_pid = 0;
                            self.status = format!(
                                "AutoInject: Enabled | trying every {} seconds",
                                self.delay_seconds
                            );
                        } else if previous_auto {
                            self.status = "AutoInject: Disabled".to_owned();
                        }
                    }

                    ui.label("Delay (s):");
                    let delay_changed = ui
                        .add_enabled(
                            !self.auto_inject,
                            egui::DragValue::new(&mut self.delay_seconds)
                                .range(1..=3600)
                                .speed(1.0),
                        )
                        .changed();
                    changed |= delay_changed;
                });
            });

            ui.add_space(10.0);
            ui.horizontal(|ui| {
                if ui.button("Inject").clicked() {
                    self.inject_current_target(false);
                }

                if ui.button("Minimize").clicked() {
                    ctx.send_viewport_cmd(egui::ViewportCommand::Minimized(true));
                }
            });

            if changed {
                self.delay_seconds = self.delay_seconds.clamp(1, 3600);
                if !self.custom_target {
                    self.process_name = DEFAULT_PROCESS_NAME.to_owned();
                }
                self.save_config();
            }

            ui.add_space(10.0);
            ui.separator();
            ui.label(egui::RichText::new(&self.status).monospace());
        });
    }
}

#[derive(Clone, Debug)]
struct PersistedConfig {
    custom_proc_name: bool,
    delay_seconds: u32,
    dll_path: String,
    proc_name: String,
}

fn load_config(path: &Path) -> Result<PersistedConfig, String> {
    let content = fs::read_to_string(path).map_err(|err| err.to_string())?;

    let mut custom_proc_name = false;
    let mut delay_seconds = 5_u32;
    let mut dll_path = DEFAULT_DLL_HINT.to_owned();
    let mut proc_name = DEFAULT_PROCESS_NAME.to_owned();

    for raw_line in content.lines() {
        let line = raw_line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }

        let Some((name, value)) = line.split_once('=') else {
            continue;
        };

        match name.trim() {
            "customProcName" => {
                let normalized = value.trim().to_ascii_lowercase();
                custom_proc_name = normalized == "true" || normalized == "1";
            }
            "delaystr" => {
                if let Ok(value) = value.trim().parse::<u32>() {
                    delay_seconds = value.clamp(1, 3600);
                }
            }
            "dllPath" => {
                dll_path = value.trim().to_owned();
            }
            "procName" => {
                proc_name = value.trim().to_owned();
            }
            _ => {}
        }
    }

    Ok(PersistedConfig {
        custom_proc_name,
        delay_seconds,
        dll_path,
        proc_name,
    })
}

fn save_config(path: &Path, cfg: &PersistedConfig) -> Result<(), String> {
    let mut content = String::new();
    content.push_str("#Fate Client injector config file\n");
    content.push_str(if cfg.custom_proc_name {
        "customProcName=true\n"
    } else {
        "customProcName=false\n"
    });
    content.push_str(&format!("delaystr={}\n", cfg.delay_seconds.clamp(1, 3600)));
    content.push_str(&format!("dllPath={}\n", cfg.dll_path));
    content.push_str(&format!("procName={}\n", cfg.proc_name));

    fs::write(path, content).map_err(|err| err.to_string())
}

#[cfg(windows)]
mod injector {
    use std::ffi::OsStr;
    use std::fs::File;
    use std::io::{self, Read};
    use std::iter;
    use std::os::windows::ffi::OsStrExt;
    use std::path::Path;
    use std::process::Command;
    use std::ptr;

    use windows_sys::Win32::Foundation::{
        CloseHandle, GetLastError, HANDLE, INVALID_HANDLE_VALUE, WAIT_FAILED, WAIT_TIMEOUT,
    };
    use windows_sys::Win32::Storage::FileSystem::{GetFileAttributesW, INVALID_FILE_ATTRIBUTES};
    use windows_sys::Win32::System::Diagnostics::ToolHelp::{
        CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W,
        TH32CS_SNAPPROCESS,
    };
    use windows_sys::Win32::System::Diagnostics::Debug::WriteProcessMemory;
    use windows_sys::Win32::System::LibraryLoader::{GetModuleHandleW, GetProcAddress};
    use windows_sys::Win32::System::Memory::{
        VirtualAllocEx, VirtualFreeEx, MEM_COMMIT, MEM_RELEASE, MEM_RESERVE, PAGE_READWRITE,
    };
    use windows_sys::Win32::System::Threading::{
        CreateRemoteThread, GetCurrentProcess, GetExitCodeThread, IsWow64Process, OpenProcess,
        WaitForSingleObject, PROCESS_CREATE_THREAD, PROCESS_QUERY_INFORMATION, PROCESS_VM_OPERATION,
        PROCESS_VM_READ, PROCESS_VM_WRITE,
    };

    const PROCESS_ACCESS: u32 = PROCESS_CREATE_THREAD
        | PROCESS_QUERY_INFORMATION
        | PROCESS_VM_OPERATION
        | PROCESS_VM_WRITE
        | PROCESS_VM_READ;

    #[derive(Copy, Clone, Eq, PartialEq)]
    enum BinaryArch {
        X86,
        X64,
    }

    pub fn get_proc_id(proc_name: &str) -> u32 {
        let requested = proc_name.trim().to_ascii_lowercase();
        if requested.is_empty() {
            return 0;
        }

        unsafe {
            let snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if snap == INVALID_HANDLE_VALUE {
                return 0;
            }

            let _snap_guard = HandleGuard::new(snap);

            let mut entry = PROCESSENTRY32W::default();
            entry.dwSize = std::mem::size_of::<PROCESSENTRY32W>() as u32;

            if Process32FirstW(snap, &mut entry) == 0 {
                return 0;
            }

            loop {
                let exe = utf16_to_string(&entry.szExeFile);
                if exe.eq_ignore_ascii_case(&requested) {
                    return entry.th32ProcessID;
                }

                if Process32NextW(snap, &mut entry) == 0 {
                    break;
                }
            }
        }

        0
    }

    pub fn run_preflight(proc_id: u32, dll_path: &Path) -> Result<(), String> {
        if proc_id == 0 {
            return Err("Invalid process ID.".to_owned());
        }

        if !dll_path.is_file() {
            return Err("DLL path does not point to a valid file.".to_owned());
        }

        let dll_wide = path_to_wide(dll_path);
        unsafe {
            if GetFileAttributesW(dll_wide.as_ptr()) == INVALID_FILE_ATTRIBUTES {
                return Err(last_error("Cannot access DLL path"));
            }
        }

        let process = unsafe { OpenProcess(PROCESS_ACCESS, 0, proc_id) };
        if process.is_null() {
            return Err(last_error("Cannot open target process with required access"));
        }
        let _process_guard = HandleGuard::new(process);

        let self_arch = detect_process_arch(unsafe { GetCurrentProcess() })?;
        let target_arch = detect_process_arch(process)?;
        if self_arch != target_arch {
            return Err("Injector and target process architectures do not match.".to_owned());
        }

        let dll_arch = detect_dll_arch(dll_path)?;
        if dll_arch != target_arch {
            return Err("DLL architecture does not match target process.".to_owned());
        }

        Ok(())
    }

    pub fn grant_uwp_read_execute(path: &Path) -> Result<(), String> {
        let status = Command::new("icacls")
            .arg(path.as_os_str())
            .arg("/grant")
            .arg("*S-1-15-2-1:(RX)")
            .status()
            .map_err(|err| format!("Failed to run icacls: {err}"))?;

        if status.success() {
            Ok(())
        } else {
            Err(format!("Failed to set DLL permissions (icacls exit code {:?}).", status.code()))
        }
    }

    pub fn perform_injection(proc_id: u32, dll_path: &Path) -> Result<(), String> {
        if proc_id == 0 {
            return Err("Invalid process ID.".to_owned());
        }

        let dll_wide = path_to_wide(dll_path);
        let bytes_to_write = dll_wide.len() * std::mem::size_of::<u16>();

        let process = unsafe { OpenProcess(PROCESS_ACCESS, 0, proc_id) };
        if process.is_null() {
            return Err(last_error("OpenProcess failed"));
        }
        let _process_guard = HandleGuard::new(process);

        let remote_memory = unsafe {
            VirtualAllocEx(
                process,
                ptr::null_mut(),
                bytes_to_write,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE,
            )
        };
        if remote_memory.is_null() {
            return Err(last_error("VirtualAllocEx failed"));
        }
        let _mem_guard = RemoteMemory::new(process, remote_memory);

        let mut bytes_written = 0;
        let write_ok = unsafe {
            WriteProcessMemory(
                process,
                remote_memory,
                dll_wide.as_ptr() as *const _,
                bytes_to_write,
                &mut bytes_written,
            )
        };
        if write_ok == 0 || bytes_written != bytes_to_write {
            return Err(last_error("WriteProcessMemory failed"));
        }

        let kernel_name = wide_literal("kernel32.dll");
        let kernel = unsafe { GetModuleHandleW(kernel_name.as_ptr()) };
        if kernel.is_null() {
            return Err(last_error("GetModuleHandleW(kernel32.dll) failed"));
        }

        let load_library = unsafe { GetProcAddress(kernel, c"LoadLibraryW".as_ptr() as *const u8) };
        let Some(load_library_ptr) = load_library else {
            return Err(last_error("GetProcAddress(LoadLibraryW) failed"));
        };

        let start_routine = Some(unsafe {
            std::mem::transmute::<
                unsafe extern "system" fn() -> isize,
                unsafe extern "system" fn(*mut core::ffi::c_void) -> u32,
            >(load_library_ptr)
        });

        let thread = unsafe {
            CreateRemoteThread(
                process,
                ptr::null(),
                0,
                start_routine,
                remote_memory,
                0,
                ptr::null_mut(),
            )
        };
        if thread.is_null() {
            return Err(last_error("CreateRemoteThread failed"));
        }
        let _thread_guard = HandleGuard::new(thread);

        let wait_code = unsafe { WaitForSingleObject(thread, 15_000) };
        if wait_code == WAIT_TIMEOUT {
            return Err("Remote thread timed out.".to_owned());
        }
        if wait_code == WAIT_FAILED {
            return Err(last_error("Failed while waiting for remote thread"));
        }

        let mut remote_module: u32 = 0;
        let exit_ok = unsafe { GetExitCodeThread(thread, &mut remote_module) };
        if exit_ok == 0 {
            return Err(last_error("GetExitCodeThread failed"));
        }
        if remote_module == 0 {
            return Err("LoadLibraryW failed inside target process.".to_owned());
        }

        Ok(())
    }

    fn detect_process_arch(process: HANDLE) -> Result<BinaryArch, String> {
        let mut wow64 = 0;
        let ok = unsafe { IsWow64Process(process, &mut wow64) };
        if ok == 0 {
            return Err(last_error("Cannot detect process architecture"));
        }

        #[cfg(target_pointer_width = "64")]
        {
            if wow64 != 0 {
                Ok(BinaryArch::X86)
            } else {
                Ok(BinaryArch::X64)
            }
        }

        #[cfg(target_pointer_width = "32")]
        {
            let _ = wow64;
            Ok(BinaryArch::X86)
        }
    }

    fn detect_dll_arch(path: &Path) -> Result<BinaryArch, String> {
        let mut file = File::open(path).map_err(|err| format!("Cannot open DLL: {err}"))?;

        let mut bytes = Vec::new();
        file.read_to_end(&mut bytes)
            .map_err(|err| format!("Cannot read DLL: {err}"))?;

        if bytes.len() < 0x40 || &bytes[0..2] != b"MZ" {
            return Err("DLL is not a valid PE image.".to_owned());
        }

        let pe_offset = u32::from_le_bytes([bytes[0x3C], bytes[0x3D], bytes[0x3E], bytes[0x3F]])
            as usize;
        if pe_offset + 6 > bytes.len() {
            return Err("DLL has an invalid PE header offset.".to_owned());
        }

        if &bytes[pe_offset..pe_offset + 4] != b"PE\0\0" {
            return Err("DLL is not a valid PE image.".to_owned());
        }

        let machine = u16::from_le_bytes([bytes[pe_offset + 4], bytes[pe_offset + 5]]);
        match machine {
            0x014c => Ok(BinaryArch::X86),
            0x8664 | 0xAA64 => Ok(BinaryArch::X64),
            _ => Err("DLL architecture is unsupported.".to_owned()),
        }
    }

    fn utf16_to_string(buffer: &[u16]) -> String {
        let end = buffer.iter().position(|&value| value == 0).unwrap_or(buffer.len());
        String::from_utf16_lossy(&buffer[..end]).to_ascii_lowercase()
    }

    fn path_to_wide(path: &Path) -> Vec<u16> {
        path.as_os_str()
            .encode_wide()
            .chain(iter::once(0))
            .collect()
    }

    fn wide_literal(value: &str) -> Vec<u16> {
        OsStr::new(value)
            .encode_wide()
            .chain(iter::once(0))
            .collect()
    }

    fn last_error(context: &str) -> String {
        let code = unsafe { GetLastError() };
        if code == 0 {
            return format!("{context}.");
        }

        let message = io::Error::from_raw_os_error(code as i32).to_string();
        format!("{context}. (error {code}: {message})")
    }

    struct HandleGuard {
        handle: HANDLE,
    }

    impl HandleGuard {
        fn new(handle: HANDLE) -> Self {
            Self { handle }
        }
    }

    impl Drop for HandleGuard {
        fn drop(&mut self) {
            if !self.handle.is_null() && self.handle != INVALID_HANDLE_VALUE {
                unsafe {
                    CloseHandle(self.handle);
                }
            }
        }
    }

    struct RemoteMemory {
        process: HANDLE,
        pointer: *mut core::ffi::c_void,
    }

    impl RemoteMemory {
        fn new(process: HANDLE, pointer: *mut core::ffi::c_void) -> Self {
            Self { process, pointer }
        }
    }

    impl Drop for RemoteMemory {
        fn drop(&mut self) {
            if !self.pointer.is_null() {
                unsafe {
                    VirtualFreeEx(self.process, self.pointer, 0, MEM_RELEASE);
                }
            }
        }
    }
}

#[cfg(not(windows))]
mod injector {
    use std::path::Path;

    pub fn get_proc_id(_proc_name: &str) -> u32 {
        0
    }

    pub fn run_preflight(_proc_id: u32, _dll_path: &Path) -> Result<(), String> {
        Err("This injector only supports Windows.".to_owned())
    }

    pub fn grant_uwp_read_execute(_path: &Path) -> Result<(), String> {
        Err("This injector only supports Windows.".to_owned())
    }

    pub fn perform_injection(_proc_id: u32, _dll_path: &Path) -> Result<(), String> {
        Err("This injector only supports Windows.".to_owned())
    }
}

fn main() -> eframe::Result<()> {
    let viewport = egui::ViewportBuilder::default()
        .with_title(APP_TITLE)
        .with_inner_size([560.0, 290.0])
        .with_resizable(true);

    let native_options = eframe::NativeOptions {
        viewport,
        ..Default::default()
    };

    eframe::run_native(
        APP_TITLE,
        native_options,
        Box::new(|cc| Ok(Box::new(FateInjectorApp::new(cc)))),
    )
}
