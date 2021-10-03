import { style } from "@vanilla-extract/css";

const azure = "azure";
const aqua = "aqua";
const dodgerblue = "dodgerblue";
const blue = "blue";

export const padCls = style({
  position: "relative",
  display: "block",
  textShadow: `0 0 10px ${azure}, 0 0 20px ${aqua}, 0 0 40px ${dodgerblue}, 0 0 80px ${blue}`,
  boxShadow: "-12px 12px 24px #d3d3d3, 12px -12px 24px #ededed",
  willChange: "filter, color",
  backgroundColor: "transparent",
  border: "none",
  borderRadius: "14px",
  width: "10rem",
  height: "10rem",
  fontSize: "2rem",
  cursor: "pointer",
  background: "#e0e0e0",
  ":hover": {
    border: "solid 0.2rem azure",
  },
  "@media": {
    "screen and (max-width: 480px)": {
      width: "4rem",
      height: "4rem",
    },
  },
});
