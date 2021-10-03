import { keyframes, style } from "@vanilla-extract/css";

const blink = keyframes({
  "0%": { border: "solid 1px #e0e0e0" },
  "100%": { border: "solid 1px azure" },
});

export const StepPadCls = style({
  width: "64px",
  height: "64px",
  margin: "0.5rem",
  background: "#e0e0e0",
  boxShadow: "-5px 5px 16px #bebebe, 5px -5px 16px #fff",
  cursor: "pointer",
  borderRadius: "8px",
});

export const StepPadPushedCls = style({
  boxShadow: "inset -5px 5px 16px #bebebe, inset 5px -5px 16px #fff",
});

export const StepPadCurrentCls = style({
  background: "#fff",
});

export const StepPadActiveCls = style({
  animation: `${blink} .5s infinite alternate`,
});
