# Controller

**Carrier + WT32-ETH01 + Ra-02, slim case** · Firmware v0.3.2

Thanks for trying the Net2RF LED controller. It takes colours from your show over the network (DDP or E1.31) and sends them over 433 MHz to RF wristbands and pucks. Power supply not included.

## Pre-flight check, before the first power-on

Parts can shift in shipping. Lift the lid (it snaps off) and:

- [ ] The **u.FL connector** on the radio module is pressed fully on: push straight down until it clicks.
- [ ] The **radio module** and the **WT32-ETH01** are seated straight in their sockets, no pins out of line.
- [ ] The **OLED lead** is plugged into the board and nothing is loose or rattling.
- [ ] Close the lid. The **antenna is screwed on firmly**, finger tight.
- [ ] Power: **5-28 V DC, centre positive** (12 V recommended). Antenna on *before* power, every time.

## Connect

1. **Ethernet (recommended):** plug in a network cable, then power on. The IP address appears on the OLED. Open `http://net2rf.local/` or that IP in a browser.
2. **No Ethernet:** after 15 s it opens a hotspot. Join **NET2RF-XXXX**, password `net2rf1234`, and open `http://192.168.4.1/`. Under **Network** pick your **2.4 GHz** Wi-Fi and **Save & reboot**.
3. For a show, give it a **static IP** or a DHCP reservation, so FPP and xLights always find it.

## Use it in your show

- **Falcon Player (10.3+):** the **Net2RF Auto Mode** plugin makes zones follow the colours of your other props, with nothing to sequence. Set **FPP Settings → UI → User Interface Level** to **Developer**, then in **Content Setup → Plugin Manager** paste the plugin's URL (below) and **Install**. Set it up under **Content Setup → Net2RF Auto Mode**.
- **xLights:** add it as a **DDP** controller at its IP, with **Keep Channel Numbers** unticked. Each zone is one pixel. The controller's **Tools → xLights setup** has the details.
- **Test without a show:** Dashboard → **Test mode**, or Tools → **Range walk**.

## Firmware updates

- Shipped with firmware **v0.3.2**. Updates install over the network: **System → Update firmware → Check for updates → Install**.
- **Keep it powered** until the OLED says **Update confirmed** (about 15 s after the restart). Unplugged sooner, it goes back to the previous version.
- Settings are kept across updates and power cuts.
- The **Unit MAC** (on the OLED at power-up, and under **System**) identifies this unit: quote it if you get in touch.

## Buttons

| | |
|---|---|
| USER, press | Next OLED page (or wakes the display) |
| USER, hold 5 s | Network reset: network settings and admin password cleared |
| USER, hold 15 s | Factory reset: everything erased |
| EN | Restart |

The OLED shows what releasing USER will do.

## Links

| QR | Link |
|---|---|
| <img src="qr/repo.svg" width="96" alt="QR code: GitHub repo"> | **GitHub repo**<br><https://github.com/mikeneiderhauser/net2rf-led> |
| <img src="qr/guide.svg" width="96" alt="QR code: User guide"> | **User guide**<br><https://github.com/mikeneiderhauser/net2rf-led/blob/main/docs/guides/user-guide.md> |
| <img src="qr/plugin.svg" width="96" alt="QR code: FPP plugin"> | **FPP plugin**<br><https://github.com/mikeneiderhauser/fpp-net2rf-auto> |
| <img src="qr/show.svg" width="96" alt="QR code: Show setup (xLights)"> | **Show setup (xLights)**<br><https://github.com/mikeneiderhauser/net2rf-led/blob/main/docs/guides/show-setup.md> |

Questions or problems: [open an issue](https://github.com/mikeneiderhauser/net2rf-led/issues).
