import { style } from "@vanilla-extract/css";

export const circleButtonCls = style({
  background:
    "linear-gradient(225deg, var(--color-gray3), var(--color-grayLight3))",
  boxShadow:
    "-5px 5px 10px var(--color-gray2), 5px -5px 10px var(--color-grayLight2)",
  color: "var(--color-gray)",
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
    boxShadow:
      "-5px 5px 10px var(--color-gray2), 5px -5px 10px var(--color-grayLight2)",
  },
});

export const circleButtonActiveCls = style({
  background: "var(--color-white)",
});
