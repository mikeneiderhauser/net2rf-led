#!/usr/bin/env python3
"""Writes the xLights custom models for a Net2RF LED controller (run from this folder).

Node 1 is the controller's All Zones zone, node N+1 is Zone N, so the model's channels line up with the
controller's zones when it is the only model on port 1. Each zone is also a named submodel.
"""
from xml.sax.saxutils import quoteattr


def model(zones: int, columns: int) -> str:
    rows = ["1" + "," * (columns - 1)]  # All Zones on the top row
    nodes = list(range(2, zones + 2))
    for i in range(0, zones, columns):
        row = [str(n) for n in nodes[i:i + columns]]
        rows.append(",".join(row + [""] * (columns - len(row))))
    names = ["All Zones"] + [f"Zone {k}" for k in range(1, zones + 1)]
    attrs = {
        "name": f"Net2RF All + {zones}", "parm1": str(columns), "parm2": str(len(rows)), "Depth": "1",
        "StringType": "RGB Nodes", "Transparency": "0", "PixelSize": "6", "ModelBrightness": "", "Antialias": "1",
        "StrandNames": "", "NodeNames": ",".join(names), "CustomModel": ";".join(rows), "SourceVersion": "2026.17",
    }
    out = ['<?xml version="1.0" encoding="UTF-8"?>', "<custommodel "]
    out.append(" ".join(f"{k}={quoteattr(v)}" for k, v in attrs.items()) + " >")
    # Port 1, with the colour order stated: without it xLights uploads its default (GRB) for the port.
    out.append('<ControllerConnection Port="1" Protocol="ws2811" colorOrder="RGB"/>')
    subs = [(n, str(i + 1)) for i, n in enumerate(names)] + [(f"Zones 1-{zones}", f"2-{zones + 1}")]
    for name, nodes_ in subs:
        out.append(f'<subModel name={quoteattr(name)} layout="horizontal" type="ranges" bufferstyle="Default" '
                   f'line0={quoteattr(nodes_)}>\n  <ControllerConnection/>\n</subModel>')
    out.append("</custommodel>")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    for zones, columns in ((4, 4), (15, 5)):
        path = f"Net2RF_All_plus_{zones}.xmodel"
        open(path, "w").write(model(zones, columns))
        print("wrote", path)
