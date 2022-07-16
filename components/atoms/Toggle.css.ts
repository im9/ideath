import { style } from "@vanilla-extract/css";

export const label = style({
  display: "inline-flex",
  alignItems: "center",
  cursor: "pointer",
  color: "#394a56",
});

export const toggle = style({
  isolation: "isolate",
  position: "relative",
  height: "30px",
  width: "60px",
  borderRadius: "15px",
  overflow: "hidden",
  boxShadow: `
    2px -2px 2px 0px var(--color-gray2),
    -2px 2px 12px 0px #d1d9e6,
    2px 2px 2px 0px #d1d9e6 inset,
    inset 2px -2px 10px var(--color-white);
  `,
});

export const toggleState = style({
  display: "none",
});

export const indicator = style({
  height: "100%",
  width: "200%",
  background: "var(--color-grayLight2)",
  borderRadius: "15px",
  transform: "translate3d(-75%, 0, 0)",
  transition: "transform 0.4s cubic-bezier(0.85, 0.05, 0.18, 1.35)",
  boxShadow: `
  -4px -4px 4px 0px #ecf0f3 inset,
  4px 4px 12px 0px #d1d9e6;
  `,
});

export const indicatorOn = style({
  transform: "translate3d(25%, 0, 0)",
});
