import { style } from "@vanilla-extract/css";

export const circleButtonCls = style({
  backgroundColor: "red",
  background: "linear-gradient(225deg, #cacaca, #f0f0f0)",
  boxShadow: "-5px 5px 10px #bebebe, 5px -5px 10px #ededed",
  color: "#636661",
  padding: "6px",
  border: "none",
  borderRadius: "100%",
  width: "40px",
  height: "40px",
  margin: "5px",
  cursor: "pointer",
  ":focus": {
    outline: "none",
  },
  ":hover": {
    boxShadow: "-5px 5px 10px #bebebe, 5px -5px 10px #eee",
  },
});
