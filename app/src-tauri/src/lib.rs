//! Super Alex Kidd Maker as a desktop app: the Maker page (maker/public) in a
//! window. The game runs in the page (WebAssembly engine).

/// "Quitter" in the game menu.
#[tauri::command]
fn quit(app: tauri::AppHandle) {
    app.exit(0);
}

pub fn run() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![quit])
        .run(tauri::generate_context!())
        .expect("error while running the app");
}
