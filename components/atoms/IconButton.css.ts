import { style, globalStyle } from "@vanilla-extract/css";

export const iconButtonCls = style({
  outline: "none",
  border: "none",
  background: "none",
  width: "62px",
  height: "62px",
  position: "relative",
  borderRadius: "18px",
  padding: "2px",
  boxShadow: "4px 2px 16px rgba(136, 165, 191, 0.48), -4px -2px 16px #ffffff",
  // -webkit-tap-highlight-color: "transparent",
});

export const visuallyHiddenCls = style({
  borderWidth: "0",
  clip: "rect(1px, 1px, 1px, 1px)",
  height: "1px",
  overflow: "hidden",
  padding: "0",
  position: "absolute",
  whiteSpace: "nowrap",
  width: "1px",
});

export const contentWrapperCls = style({
  cursor: "pointer",
  width: "100%",
  height: "100%",
  borderRadius: "calc(18px - 2px)",
  transition: "all 0.2s ease-in-out",
  display: "grid",
  placeItems: "center",
  color: "var(--color-gray)",
});

export const iconCls = style({
  userSelect: "none",
  transition: "all 0.2s ease-in-out",
  fontSize: "calc((62px / 2))",
  position: "relative",
  pointerEvents: "none",
  color: "rgba(201, 215, 230, 0.8)",
  textShadow: "2px 2px 2px rgba(214, 225, 239, 0.6), 0 0 0 #000",
});

export const contentWrapperCheckedCls = style({
  boxShadow:
    "inset 3px 3px 7px rgba(136, 165, 191, 0.48), inset -3px -3px 7px #ffffff",
});

globalStyle(`${contentWrapperCheckedCls} > i > svg`, {
  fill: "#ccc",
});
