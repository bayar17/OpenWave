# CSR Bluetooth USB passthrough

The CSR `0a12:0001` dongle receives a new `/dev/bus/usb/...` node after each
disconnect. A temporary `setfacl` grant does not follow the new node. Install
the host rule once to give members of `plugdev` access to each new node:

```sh
sudo install -m 0644 host/udev/70-openwave-csr-bluetooth.rules \
  /etc/udev/rules.d/70-openwave-csr-bluetooth.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --action=change --subsystem-match=usb \
  --attr-match=idVendor=0a12 --attr-match=idProduct=0001
sudo udevadm settle
```

Run `./run-openwave check` to confirm access. This rule
only fixes host USB permissions; it does not prevent the hardware from
disconnecting. For a temporary grant on the current node, use the path printed
by `./run-openwave check` with `sudo setfacl -m u:$(id -un):rw PATH`.

# Samsung Wi-Fi USB passthrough

OpenWave automatically passes the Samsung `04e8:2018` Wi-Fi adapter to QEMU
when it is connected. Install this host rule so `plugdev` members retain
access after the adapter is replugged or its USB device number changes:

```sh
sudo install -m 0644 host/udev/70-openwave-samsung-wifi.rules \
  /etc/udev/rules.d/70-openwave-samsung-wifi.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --action=change --subsystem-match=usb \
  --attr-match=idVendor=04e8 --attr-match=idProduct=2018
sudo udevadm settle
```

Run `./run-openwave check` to verify passthrough. Use
`--usb-wifi none` when you intentionally want to boot without the adapter.
