import { keyframes, style } from "@vanilla-extract/css";

const animate1 = keyframes({
  "0%": { left: "-100%" },
  "50%": { left: "100%" },
  "100%": { left: "100%" },
});

const animate2 = keyframes({
  "0%": { top: "-100%" },
  "50%,100%": { top: "100%" },
});

const animate3 = keyframes({
  "0%": { right: "-100%" },
  "50%,100%": { right: "100%" },
});

const animate4 = keyframes({
  "0%": { bottom: "-100%" },
  "50%,100%": { bottom: "100%" },
});

export const PlayButtonCls = style({
  width: "118px",
  height: "68px",
  background: "#e0e0e0",
  boxShadow: "-5px 5px 10px #bebebe, 5px -5px 10px #fff",
  position: "relative",
  display: "inline-block",
  padding: "25px 30px",
  color: "#636661",
  textDecoration: "none",
  textTransform: "uppercase",
  transition: "0.5s",
  letterSpacing: "4px",
  overflow: "hidden",
  cursor: "pointer",
  borderRadius: "4px",
  ":hover": {
    color: "#ccc",
    boxShadow: "-5px 5px 12px #bebebe, 5px -5px 12px #eee",
  },
});

export const PlayButtonPushedCls = style({
  boxShadow: "inset -5px 5px 10px #bebebe, inset 5px -5px 10px #fff",
});

export const PlayButtonPlayCls = style({
  color: "#03e9f4",
  background: "#f8f8ff",
  ":hover": {
    color: "#ccc",
    boxShadow:
      "0 0 1px #03e9f4, 0 0 6px #03e9f4, 0 0 12px #cacfcf, 0 0 20px #03e9f4",
  },
  selectors: {
    "&:nth-child(1)": {
      filter: "hue-rotate(270deg)",
    },
    "&:nth-child(2)": {
      filter: "hue-rotate(110deg)",
    },
  },
});

export const PlayButtonPlayTopBorderCls = style({
  position: "absolute",
  display: "block",
  top: "0",
  left: "0",
  width: "100%",
  height: "2px",
  background: "linear-gradient(90deg, transparent, #03e9f4)",
  animation: `${animate1} 1s linear infinite`,
});

export const PlayButtonPlayRightBorderCls = style({
  position: "absolute",
  display: "block",
  top: "-100%",
  right: "0",
  width: "2px",
  height: "100%",
  background: "linear-gradient(180deg, transparent, #03e9f4)",
  animation: `${animate2} 1s linear infinite`,
  animationDelay: "0.25s",
});

export const PlayButtonPlayBottomBorderCls = style({
  position: "absolute",
  display: "block",
  bottom: "0",
  right: "0",
  width: "100%",
  height: "2px",
  background: "linear-gradient(270deg, transparent, #03e9f4)",
  animation: `${animate3} 1s linear infinite`,
  animationDelay: "0.5s",
});

export const PlayButtonPlayLeftBorderCls = style({
  position: "absolute",
  display: "block",
  bottom: "-100%",
  left: "0",
  width: "2px",
  height: "100%",
  background: "linear-gradient(360deg, transparent, #03e9f4)",
  animation: `${animate4} 1s linear infinite`,
  animationDelay: "0.75s",
});
