import { style } from "@vanilla-extract/css";

export const SquareButtonCls = style({
  background: "var(--color-grayLight)",
  boxShadow:
    "-5px 5px 10px var(--color-gray2), 5px -5px 10px var(--color-white)",
  color: "var(--color-gray)",
  border: "none",
  borderRadius: "4px",
  width: "118px",
  height: "68px",
  margin: "0.5rem auto",
  cursor: "pointer",
  position: "relative",
  display: "inline-block",
  textDecoration: "none",
  textTransform: "uppercase",
  transition: "0.5s",
  letterSpacing: "4px",
  overflow: "hidden",
  ":focus": {
    outline: "none",
  },
  ":hover": {
    color: "#ccc",
    boxShadow:
      "-5px 5px 10px var(--color-gray2), 5px -5px 10px var(--color-grayLight2)",
  },
});

export const SquareButtonSmallCls = style({
  height: "34px",
});
