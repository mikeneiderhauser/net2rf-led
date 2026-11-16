# Carrier board

**Assembled board, R2.0** · For firmware v0.3.2 or later

Thanks for trying the Net2RF LED carrier board. The WT32-ETH01, a Ra-02 radio and a 0.96" OLED plug in to make a controller that drives 433 MHz RF wristbands and pucks from your light show. The R2.0 fixes (SPI bridge, reset capacitor) are already fitted.

## You'll need

- **WT32-ETH01** (the original, not the EVO) with its pins on the **component side, the same side as the RJ45 jack**. Most pre-soldered modules have them on the other side and won't fit.
- **Ra-02 (SX1278, 433 MHz)** on an adapter with two 1×8 rows of 2.54 mm pins.
- **0.96" SSD1306 I²C OLED**, pads in the order **GND, VCC, SCL, SDA**, and a 4-pin JST-PH 2.0 lead. VCC first will damage it.
- 433 MHz antenna, u.FL to SMA pigtail, and a **5-28 V DC** supply, centre positive.
- A 3.3 V USB-serial adapter or WT32 programmer for the **first** flash only.

## Assemble

1. Plug the OLED lead into **J7** first: it sits under the radio.
2. WT32-ETH01 **component side down**, RJ45 through the cut-out.
3. Radio adapter into J9/J10, module facing up. Press the u.FL on until it clicks.
4. Print a case (slim, or with room for an amplifier): STLs linked below.

## First flash

1. Open the **web flasher** (QR below) in Chrome or Edge.
2. Connect the adapter at **3.3 V**: GND, TX→RXD0, RX→TXD0. Power the board from its own supply.
3. **Hold BOOT, tap EN, release BOOT**, then click **Install**. Tap EN when it's done.
4. After this, updates go over the network: no cable needed.

## Pre-flight check, before powering on

- [ ] Both modules seated straight; u.FL pressed fully on; OLED lead in.
- [ ] **Antenna screwed on firmly** before power.
- [ ] Supply is 5-28 V DC, centre positive.

## Connect

1. **Ethernet (recommended):** plug in a network cable, then power on. The IP address appears on the OLED. Open `http://net2rf.local/` or that IP in a browser.
2. **No Ethernet:** after 15 s it opens a hotspot. Join **NET2RF-XXXX**, password `net2rf1234`, and open `http://192.168.4.1/`. Under **Network** pick your **2.4 GHz** Wi-Fi and **Save & reboot**.
3. For a show, give it a **static IP** or a DHCP reservation, so FPP and xLights always find it.

## Use it in your show

- **Falcon Player (10.3+):** the **Net2RF Auto Mode** plugin makes zones follow the colours of your other props, with nothing to sequence. Set **FPP Settings → UI → User Interface Level** to **Developer**, then in **Content Setup → Plugin Manager** paste the plugin's URL (below) and **Install**. Set it up under **Content Setup → Net2RF Auto Mode**.
- **xLights:** add it as a **DDP** controller at its IP, with **Keep Channel Numbers** unticked. Each zone is one pixel. The controller's **Tools → xLights setup** has the details.
- **Test without a show:** Dashboard → **Test mode**, or Tools → **Range walk**.

## Links

| QR | Link |
|---|---|
| <img src="qr/repo.svg" width="96" alt="QR code: GitHub repo"> | **GitHub repo**<br><https://github.com/mikeneiderhauser/net2rf-led> |
| <img src="qr/carrier.svg" width="96" alt="QR code: Carrier board guide"> | **Carrier board guide**<br><https://github.com/mikeneiderhauser/net2rf-led/blob/main/docs/build/carrier-board.md> |
| <img src="qr/flasher.svg" width="96" alt="QR code: Web flasher"> | **Web flasher**<br><https://mikeneiderhauser.github.io/net2rf-led/> |
| <img src="qr/case.svg" width="96" alt="QR code: Case STLs"> | **Case STLs**<br><https://github.com/mikeneiderhauser/net2rf-led/tree/main/hardware/case> |
| <img src="qr/plugin.svg" width="96" alt="QR code: FPP plugin"> | **FPP plugin**<br><https://github.com/mikeneiderhauser/fpp-net2rf-auto> |

Questions or problems: [open an issue](https://github.com/mikeneiderhauser/net2rf-led/issues).
