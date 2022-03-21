import { style } from "@vanilla-extract/css";

export const circleButtonCls = style({
  background:
    "linear-gradient(225deg, var(--color-gray3), var(--color-grayLight3))",
  boxShadow:
    "-5px 5px 10px var(--color-gray2), 5px -5px 10px var(--color-grayLight2)",
  color: "var(--color-gray)",
  border: "none",
  borderRadius: "100%",
  width: "40px",
  height: "40px",
  margin: "5px",
  padding: "6px",
  cursor: "pointer",
  ":focus": {
    outline: "none",
  },
  ":hover": {
    boxShadow:
      "-5px 5px 10px var(--color-gray2), 5px -5px 10px var(--color-grayLight2)",
  },
});

export const circleButtonActiveCls = style({
  background: "var(--color-white)",
});

export const circleButtonSmallCls = style({
  width: "24px",
  height: "24px",
  padding: "2px",
});
