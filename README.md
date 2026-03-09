# Fate Injector
The Fate Client themed UWP app DLL Injector.

Latest Injector [Download](https://github.com/FR0NDAY/FateInjector/releases/download/fateinjectorrust/fateInjector.exe)

Get Fate Client DLL on the Fate Client [Repository](https://github.com/fligger/FateClient)

#### Because PC viruses may use simular code to infect computers this might get reconized as a virus. 
#### BUT this injector does not contain any viruses! Feel free to check the source!

## Features:
- Auto inject
- Adds the "All Applications Packages" permission to the DLL so UWP apps can load it
- Rust-only codebase and build pipeline

Scuffed Youtube Devlog (Version 0.9) [here](https://www.youtube.com/watch?v=_50QBD4pKEs&list=PLVRYtYhvPXj5J6IwIFAAFO8CrpgmsLFki&index=4)

## Build (Rust)
1. Install Rust (stable): https://rustup.rs
2. Run: `cargo build --release`
3. Executable output: `target/release/fateInjector.exe`

## GitHub Actions
The workflow in `.github/workflows/build-windows.yml` builds and uploads:
- `target/release/fateInjector.exe`
