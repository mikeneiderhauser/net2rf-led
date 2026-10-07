# Post-build: write firmware.factory.bin (bootloader + partitions + app) for first-time USB flashing at 0x0.
Import("env")  # noqa: F821


def merge_factory(source, target, env):
    import os
    import subprocess
    build_dir = env.subst("$BUILD_DIR")
    app = os.path.join(build_dir, "firmware.bin")
    out = os.path.join(build_dir, "firmware.factory.bin")
    chip = env.BoardConfig().get("build.mcu", "esp32")
    # The ESP32's ROM loads the bootloader from 0x1000; the S3 / C3 / C6 families from 0x0.
    boot_off = "0x1000" if chip == "esp32" else "0x0"
    images = [(hex(int(off, 0)), img) for img, off in [
        (os.path.join(build_dir, "bootloader.bin"), boot_off),
        (os.path.join(build_dir, "partitions.bin"), "0x8000"),
    ]]
    boot_app0 = os.path.join(env.PioPlatform().get_package_dir("framework-arduinoespressif32"),
                             "tools", "partitions", "boot_app0.bin")
    if os.path.exists(boot_app0):
        images.append(("0xe000", boot_app0))
    images.append(("0x10000", app))
    args = [env.subst("$PYTHONEXE"), "-m", "esptool", "--chip", chip, "merge-bin", "-o", out]
    for off, img in images:
        args += [off, img]
    if subprocess.call(args) != 0:
        # older esptool spelling
        args[args.index("merge-bin")] = "merge_bin"
        subprocess.check_call(args)
    print(f"Factory image: {out}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_factory)  # noqa: F821
