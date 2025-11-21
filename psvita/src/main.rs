extern crate sdl2;

use std::ffi::CString;
use std::os::raw::{c_char, c_int, c_void};

unsafe extern "C" {
    fn external_main(argc: c_int, argv: *const *const c_char, external_window: *mut c_void, external_renderer: *mut c_void) -> c_int;
}

fn main() -> Result<(), String> {
    let sdl_context = sdl2::init()?;
    let video_subsystem = sdl_context.video()?;

    let width = 960;
    let height = 544;
    let window = video_subsystem
        .window("ARMSX PSVita Host", width, height)
        .position_centered()
        .build()
        .map_err(|e| e.to_string())?;

    let canvas = window
        .into_canvas()
        .present_vsync()
        .accelerated()
        .build()
        .map_err(|e| e.to_string())?;

    let args = vec!["psvita", "", ""];
    let cstrings: Vec<CString> = args.iter().map(|s| CString::new(*s).unwrap()).collect();
    let mut ptrs: Vec<*const c_char> = cstrings.iter().map(|s| s.as_ptr()).collect();
    ptrs.push(std::ptr::null());

    let raw_window = canvas.window().raw() as *mut c_void;
    let raw_renderer = canvas.raw() as *mut c_void;

    let ret = unsafe { external_main((ptrs.len() - 1) as c_int, ptrs.as_ptr(), raw_window, raw_renderer) };

    if ret != 0 {
        return Err(format!("external_main returned non-zero: {}", ret));
    }

    Ok(())
}