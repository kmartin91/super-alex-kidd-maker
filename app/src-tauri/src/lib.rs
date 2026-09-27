//! Super Alex Kidd Maker as a desktop app: the Maker page (maker/public) in a
//! window. The game runs in the page (WebAssembly engine). The app updates
//! itself from the GitHub releases, and opens links to online levels
//! (superalexkiddmaker://play/CODE, from the level gallery of the website).

use tauri::Manager;

/// "Quitter" in the game menu.
#[tauri::command]
fn quit(app: tauri::AppHandle) {
    app.exit(0);
}

pub fn run() {
    tauri::Builder::default()
        // A second launch (a link opened while the app runs, on Windows and
        // Linux) hands its link to the running app instead; it must come first.
        .plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| {
            if let Some(window) = app.get_webview_window("main") {
                let _ = window.set_focus();
            }
        }))
        .plugin(tauri_plugin_deep_link::init())
        // Updates: checked by the page (maker/public/js/updates.js) against the
        // releases on GitHub, signed with the key in ~/.tauri (app/RELEASE.md).
        .plugin(tauri_plugin_updater::Builder::new().build())
        .plugin(tauri_plugin_process::init())
        .setup(|app| {
            // Linux AppImages and development builds register the link scheme
            // themselves (installers do it on Windows, the bundle on macOS).
            #[cfg(any(target_os = "linux", all(debug_assertions, windows)))]
            {
                use tauri_plugin_deep_link::DeepLinkExt;
                let _ = app.deep_link().register_all();
            }
            let _ = app;
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![quit])
        .run(tauri::generate_context!())
        .expect("error while running the app");
}
