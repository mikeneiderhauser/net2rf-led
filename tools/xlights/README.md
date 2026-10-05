# xLights files for Net2RF LED

| File | What it is |
|---|---|
| `net2rf.xcontroller` | Controller definition: vendor **Net2RF**, model **Net2RF LED**. One pixel port, DDP or E1.31, up to 48 channels (16 zones). Upload uses xLights' WLED driver, which the controller answers. |
| `Net2RF_All_plus_4.xmodel` | Custom model, 5 nodes: All Zones + Zone 1-4. Matches a new controller's default zones. |
| `Net2RF_All_plus_15.xmodel` | Custom model, 16 nodes: All Zones + Zone 1-15, the controller's maximum. |
| `make_models.py` | Regenerates the two models. |

Both models are **RGB**: String Type *RGB Nodes*, and their controller connection is preset to port 1 with
colour order RGB, which is the controller's default colour order.

How the models map: node 1 is the controller's first zone (All Zones) and node N+1 is Zone N. Each is a
submodel with that name, plus **Zones 1-N** for every single zone together. Sequencing and what the base layer
does: [docs/SETUP.md](../../docs/SETUP.md#all-zones-as-a-base-layer-protocol-0).

## Installing the controller definition

xLights only reads controller definitions from its own `controllers` folder, so until the file is part of
xLights it has to be copied there, and copied again after each xLights update:

| System | Folder |
|---|---|
| macOS | `/Applications/xLights.app/Contents/Resources/controllers/` (not possible with the Mac App Store version, which macOS protects from changes) |
| Windows | `C:\Program Files\xLights\controllers\` |
| Linux | the `controllers` folder next to xLights' resources |

Restart xLights, then pick **Net2RF** / **Net2RF LED** on the controller. Without the file, use
**WLED / WLED / Generic ESP32**: sending and upload both work with it; it just shows 8 ports.

## Using a model

1. *Layout* tab: import the `.xmodel` as a custom model and place it.
2. Set its controller to the Net2RF controller, **port 1**, protocol ws2811. It must be the only model on the
   port.
3. *Controllers* tab: **Upload Output** sets the controller's zone count to the model's node count. It does
   not change the colour order: keep the controller's colour order equal to the model's String Type (RGB).
4. Sequence the submodels.
