# Controller with amplifier

**Carrier + WT32-ETH01 + Ra-02 + 433 MHz amp** · Sheet rev B · 2026-11-16

Thanks for trying the Net2RF LED controller with the external amplifier, for more range. It takes colours from your show over the network and sends them over 433 MHz to RF wristbands and pucks. Power supply not included: use **12 V DC, 2 A or more**, centre positive.

## Pre-flight check, before the first power-on

Parts can shift in shipping. Lift the lid (it snaps off) and:

- [ ] The **u.FL connector** on the radio module is pressed fully on: push straight down until it clicks.
- [ ] The **radio module** and the **WT32-ETH01** are seated straight in their sockets, no pins out of line.
- [ ] The **OLED lead** is plugged into the board and nothing is loose or rattling.
- [ ] Close the lid. The **antenna is screwed on firmly**, finger tight.
- [ ] The **radio-to-amp cable** is tight at both ends, and the **buck module's wires** are firm.
- [ ] **Never power the amp without the antenna** (or a 50 Ω load) on its output.
- [ ] Power: **5-28 V DC, centre positive** (12 V recommended). Antenna on *before* power, every time.

## Connect

1. **Ethernet (recommended):** plug in a network cable, then power on. The IP address appears on the OLED. Open that IP, or `http://net2rf-xxxx.local/` (xxxx = the last 4 digits of the Unit MAC, also in the hotspot name). Some Android phones can't open `.local` names: the IP always works.
2. **No Ethernet:** after 15 s it opens a hotspot. Join **NET2RF-XXXX**, password `net2rf1234`, and open `http://192.168.4.1/`. Under **Network** pick your **2.4 GHz** Wi-Fi and **Save & reboot**.
3. For a show, give it a **static IP** or a DHCP reservation, so FPP and xLights always find it.
4. **Secure it:** set an **admin password** (System) and change the **hotspot password** (Network → Setup access point). The default above is public.

## Use it in your show

- **Falcon Player (10.3+):** the **Net2RF Auto Mode** plugin makes zones follow the colours of your other props, with nothing to sequence. Set **FPP Settings → UI → User Interface Level** to **Developer**, then in **Content Setup → Plugin Manager** paste the plugin's URL (below) and **Install**. Set it up under **Content Setup → Net2RF Auto Mode**.
- **xLights:** add it as a **DDP** controller at its IP, with **Keep Channel Numbers** unticked. Each zone is one pixel. The controller's **Tools → xLights setup** has the details.
- **Check your devices:** Tools → **Which protocol is my device?** lights them red (protocol 0) or green (protocol 1). No response: wake the device (its button), move closer, and see the compatibility list in the repo.
- **Placement:** antenna upright, high and clear of metal. The case isn't weatherproof: mount it under cover or inside an enclosure.

## Firmware updates

- Updates install over the network: **System → Update firmware → Check for updates → Install**. The current version is on the System page and the OLED.
- **Keep it powered** until the OLED says **Update confirmed** (about 15 s after the restart). Unplugged sooner, it goes back to the previous version.
- Settings are kept across updates and power cuts.
- The **Unit MAC** (on the OLED at power-up, and under **System**) identifies this unit: quote it if you get in touch.

## The amplifier

- It's **powered only while RF output is on**. The dashboard shows **Amplifier power**: on, or why it's off. Settings: **Tools → Amplifier power**.
- **Start with low TX power** (Devices & Radio) and raise it only until every device responds. More power means more interference with neighbours, car key fobs and nearby audio gear.
- Speakers near the antenna may **buzz** during transmissions: move the antenna a few metres away.

## Links

| QR | Link |
|---|---|
| <img src="qr/repo.svg" width="96" alt="QR code: GitHub repo"> | **GitHub repo**<br><https://github.com/mikeneiderhauser/net2rf-led> |
| <img src="qr/guide.svg" width="96" alt="QR code: User guide"> | **User guide**<br><https://github.com/mikeneiderhauser/net2rf-led/blob/main/docs/guides/user-guide.md> |
| <img src="qr/plugin.svg" width="96" alt="QR code: FPP plugin"> | **FPP plugin**<br><https://github.com/mikeneiderhauser/fpp-net2rf-auto> |
| <img src="qr/amp.svg" width="96" alt="QR code: Amplifier guide"> | **Amplifier guide**<br><https://github.com/mikeneiderhauser/net2rf-led/blob/main/docs/build/amplifier.md> |
| <img src="qr/show.svg" width="96" alt="QR code: Show setup (xLights)"> | **Show setup (xLights)**<br><https://github.com/mikeneiderhauser/net2rf-led/blob/main/docs/guides/show-setup.md> |

Questions or problems: [open an issue](https://github.com/mikeneiderhauser/net2rf-led/issues).
