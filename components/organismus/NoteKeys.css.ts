import { style, keyframes } from "@vanilla-extract/css";

const blink = keyframes({
  "0%": { border: "solid 2px var(--color-grayLight)" },
  "100%": { border: "solid 2px azure" },
});

const blinkBlack = keyframes({
  "0%": { border: "solid 3px var(--color-grayLight)" },
  "100%": { border: "solid 3px azure" },
});

export const whiteKey = style({
  position: "relative",
  width: "42px",
  height: "110px",
  border: "1px solid rgba(255, 255, 255, 0.2)",
  boxSizing: "border-box",
  boxShadow:
    "-6px 6px 16px var(--color-gray2), 6px -6px 16px var(--color-white)",
  borderRadius: "50px",
});

export const whiteKeyPushed = style({
  border: "1px solid var(--color-white)",
  willChange: "color",
  animation: `${blink} .5s infinite alternate`,
});

export const whiteKeyBlock = style({
  position: "relative",
  width: "64px",
  height: "132px",
  display: "flex",
  alignItems: "center",
  justifyContent: "center",
  borderRadius: "4px",
});

export const whitekeysArea = style({
  display: "flex",
  justifyContent: "space-between",
  gridColumn: 1,
  gridRow: 2,
});

export const blackKey = style({
  position: "relative",
  width: "40px",
  height: "40px",
  borderRadius: "100%",
  background: "var(--color-gray)",
  border: "2px solid #e5e4eb",
  boxShadow: "6px 6px 16px #9fa6b3, -6px -6px 16px #e8ebf3",
  transform: "translateX(16px)",
});

export const blackKeyPushed = style({
  border: "1px solid var(--color-white)",
  willChange: "color",
  animation: `${blinkBlack} .5s infinite alternate`,
});

export const blackKeyBlock = style({
  width: "98px",
  height: "64px",
  display: "flex",
  alignItems: "center",
  justifyContent: "center",
  borderRadius: "4px",
});

export const blackkeysArea1 = style({
  display: "flex",
  justifyContent: "space-between",
  gridColumn: 1,
  gridRow: 2,
});
