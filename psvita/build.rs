fn main() {
    let target = std::env::var("TARGET").unwrap();
    if target.contains("vita") {
        let vitasdk = std::env::var("VITASDK").expect("VITASDK environment variable must be set");
        println!("cargo:rustc-link-search=native={}/arm-vita-eabi/lib", vitasdk);
        println!("cargo:rustc-link-lib=SDL2");
        println!("cargo:rustc-link-lib=SceGxm_stub");
        println!("cargo:rustc-link-lib=SceDisplay_stub");
        println!("cargo:rustc-link-lib=SceAudio_stub");
        println!("cargo:rustc-link-lib=SceAudioIn_stub");
        println!("cargo:rustc-link-lib=SceCtrl_stub");
        println!("cargo:rustc-link-lib=SceHid_stub");
        println!("cargo:rustc-link-lib=SceMotion_stub");
        println!("cargo:rustc-link-lib=SceIme_stub");
        println!("cargo:rustc-link-lib=SceCommonDialog_stub");
        println!("cargo:rustc-link-lib=SceTouch_stub");
        println!("cargo:rustc-link-lib=SceLibKernel_stub");
    }
}