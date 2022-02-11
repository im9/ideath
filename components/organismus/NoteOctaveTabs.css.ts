import { style, globalStyle } from "@vanilla-extract/css";

export const segmentedControl = style({
  width: "fit-content",
  height: "4rem",
  boxShadow:
    "-6px 6px 12px var(--color-grayLight4), 6px -6px 12px var(--color-white)",
  borderRadius: "1rem",
  display: "flex",
  alignItems: "center",
  position: "relative",
  color: "var(--color-gray)",
});

globalStyle(`${segmentedControl} > div > label`, {
  fontSize: "1rem",
});

globalStyle(`${segmentedControl} > div > input`, {
  display: "none",
});

globalStyle(`${segmentedControl} > div > input:checked`, {
  outline: "none",
});

globalStyle(`${segmentedControl} > div > input:checked + label`, {
  transition: "all .5s ease",
  color: "var(--primary)",
});

export const tab = style({
  width: "6.8rem",
  height: "3.6rem",
  fontSize: "1.4rem",
  display: "flex",
  justifyContent: "center",
  alignItems: "center",
  cursor: "pointer",
  color: "var(--greyDark)",
  transition: "all .2s ease",
  ":hover": {
    color: "var(--primary)",
  },
  ":checked": {
    transform: "translateX(0)",
    transition: "transform 0.2s cubic-bezier(0.645, 0.045, 0.355, 1)",
  },
});

export const segmentedControlChecked = style({
  position: "relative",
  height: "3.4rem",
  width: "6.2rem",
  margin: "0 .3rem",
  borderRadius: ".8rem",
  boxShadow:
    "inset -5px 5px 10px var(--color-grayLight4), inset 5px -5px 10px var(--color-white)",
  pointerEvents: "none",
});

export const settingLabel = style({
  display: "inline-block",
  fontSize: "0.5rem",
  color: "var(--color-gray)",
  background: "var(--color-white)",
  padding: "0 8px",
  borderRadius: "8px",
  margin: ".5rem 0",
});
