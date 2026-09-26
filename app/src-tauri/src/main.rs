// No console window next to the app on Windows (release builds).
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    super_alex_kidd_maker_lib::run()
}
