import { style } from "@vanilla-extract/css";

export const slider = style({
  gridColumn: "3 / 4",
  gridRow: "5 / 6",
  alignSelf: "center",
  display: "flex",
  flexDirection: "column",
});

export const sliderBox = style({
  width: "100%",
  height: "1rem",
  cursor: "pointer",
  boxShadow:
    "inset 3px 3px 7px rgba(136, 165, 191, 0.48), inset -3px -3px 7px #ffffff",
  borderRadius: "1rem",
  position: "relative",
  display: "flex",
  justifyContent: "center",
  alignItems: "center",
});

export const sliderBtn = style({
  width: "2rem",
  height: "2rem",
  borderRadius: "50%",
  background: "var(--color-white)",
  position: "absolute",
  boxShadow: "0px .1rem .3rem 0px var(--color-gray2)",
  zIndex: 200,
  display: "flex",
  justifyContent: "center",
  alignItems: "center",
  ":after": {
    content: "",
    position: "absolute",
    width: ".8rem",
    height: ".8rem",
    borderRadius: "50%",
    boxShadow:
      "inset 3px 3px 7px rgba(136, 165, 191, 0.48), inset -3px -3px 7px #ffffff",
  },
});

export const sliderColor = style({
  height: "100%",
  width: "50%",
  position: "absolute",
  left: 0,
  zIndex: 100,
  borderRadius: "inherit",
  background: "linear-gradient(-1deg, #03e9f4 0%, #03e9f4 50%, #03e9f4 100%)",
  filter: "hue-rotate(270deg)",
});

// TODO hover: opacity
export const sliderTooltip = style({
  position: "absolute",
  top: "2.6rem",
  height: "2.5rem",
  width: "3rem",
  borderRadius: ".6rem",
  display: "flex",
  justifyContent: "center",
  alignItems: "center",
  fontSize: "1.2rem",
  // color: var(--primary),
  boxShadow:
    "inset 3px 3px 7px rgba(136, 165, 191, 0.48), inset -3px -3px 7px #ffffff",
  opacity: 0,
  transition: "opacity .3s ease",
});
