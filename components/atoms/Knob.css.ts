import { style } from "@vanilla-extract/css";

export const knobWrapperCls = style({
  display: "inline-grid",
  justifyItems: "center",
  alignItems: "center",
});

export const knobInnerCls = style({
  background:
    "linear-gradient(225deg, var(--color-grayLight3), var(--color-gray3))",
  boxShadow:
    "-5px 5px 10px var(--color-gray2), 5px -5px 10px var(--color-grayLight2)",
  borderRadius: "50%",
  width: "60px",
  height: "60px",
});

export const knobCls = style({
  position: "relative",
  border: "none",
  borderRadius: "50%",
  margin: "1px",
  width: "60px",
  height: "60px",
  ":focus": {
    outline: "none",
  },
  cursor: "pointer",
  transform: "rotate(0deg)",
});

export const knobLabelCls = style({
  fontSize: "0.5rem",
  color: "var(--color-gray)",
  background: "var(--color-white)",
  padding: "0 8px",
  borderRadius: "8px",
  zIndex: 1,
});

export const knobPointerCls = style({
  position: "absolute",
  top: "calc(50% - 4px)",
  left: "10px",
  borderRadius: "50%",
  width: "8px",
  height: "8px",
  backgroundColor: "var(--color-white)",
  content: "",
});

export const knobPointerActiveCls = style({
  backgroundColor: "#7fd8e4",
  filter: "hue-rotate(270deg)",
});
