import { style } from "@vanilla-extract/css";

export const knobCls = style({
  position: "relative",
  border: "4px solid #69c0cc",
  borderRadius: "50%",
  padding: "0",
  width: "80px",
  height: "80px",
  background: "#7fd8e4",
  cursor: "pointer",
  ":before": {
    position: "absolute",
    top: "calc(50% - 4px)",
    right: "10px",
    borderRadius: "50%",
    width: "8px",
    height: "8px",
    backgroundColor: "rgba(255, 255, 255, 0.6)",
    content: "",
  },
});
