# Compile
```sh
$ ./make.sh myinit.c
```

# Change init program at boot time with GRUB
You can change which **init program** (or any kernel parameter, really) your Linux system uses by editing the **GRUB boot entry at boot time**. You don’t have to rebuild GRUB config for a one-time test; you can override it interactively.

Here’s how it works step by step:

---

### 1. Interrupt GRUB

* When your machine starts, the GRUB menu usually flashes quickly.
* If it’s hidden, press and **hold `Shift` (BIOS systems)** or **tap `Esc` (UEFI systems)** right after the BIOS splash to show the GRUB menu.
* If the menu already shows (on some distros it does), you don’t need to press anything.

---

### 2. Edit the boot entry

* In the GRUB menu, highlight the entry you want (usually the first, your normal kernel).
* Press **`e`** to edit it.

---

### 3. Modify the kernel line

* You’ll see a little editor with lines starting like:

  ```
  linux   /boot/vmlinuz-6.x.y root=/dev/... ro quiet splash
  ```
* That line is the **kernel command line**.
* At the end, you can append:

  ```
  init=/path/to/your/init
  ```

  Example:

  ```
  linux   /boot/vmlinuz-6.x.y root=/dev/sda1 ro init=/bin/bash
  ```
* This tells the kernel: instead of running `/sbin/init` (or systemd), run your custom program.

---

### 4. Boot with it

* After editing, press **`Ctrl`+`x`** or **`F10`** to boot with this temporary config.
* The system will now boot, and when the kernel finishes its setup, it will launch the init program you specified.

---

**Notes**

* If you set `init=/bin/bash`, you’ll just drop to a shell — good for rescue/debug, but no services or login manager will start.
