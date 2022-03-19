import { keyframes, style } from "@vanilla-extract/css";

const blink = keyframes({
  "0%": { border: "solid 1px var(--color-grayLight)" },
  "100%": { border: "solid 1px azure" },
});

export const StepPadCls = style({
  width: "64px",
  height: "64px",
  margin: "0.5rem",
  background: "var(--color-grayLight)",
  boxShadow:
    "-5px 5px 16px var(--color-gray2), 5px -5px 16px var(--color-white)",
  cursor: "pointer",
  borderRadius: "8px",
  "@media": {
    "screen and (max-width: 480px)": {
      width: "16px",
      height: "16px",
      margin: "0.2rem",
      borderRadius: "4px",
      boxShadow:
        "-2px 2px 8px var(--color-gray2), 2px -2px 8px var(--color-white)",
    },
  },
});

export const StepPadPushedCls = style({
  boxShadow:
    "inset -5px 5px 16px var(--color-gray2), inset 5px -5px 16px var(--color-white)",
  "@media": {
    "screen and (max-width: 480px)": {
      boxShadow:
        "inset -2px 2px 8px var(--color-gray2), inset 2px -2px 8px var(--color-white)",
    },
  },
});

export const StepPadCurrentCls = style({
  background: "var(--color-white)",
});

export const StepPadActiveCls = style({
  animation: `${blink} .5s infinite alternate`,
});
