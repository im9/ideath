import { style, globalStyle } from "@vanilla-extract/css";

export const containerCls = style({
  height: "100%",
  backgroundColor: "var(--color-grayLight)",
});

export const titleCls = style({
  color: "var(--color-white)",
  fontSize: "4rem",
  fontWeight: 300,
  "@media": {
    "screen and (max-width: 480px)": {
      fontSize: "3rem",
    },
  },
});

export const heroImage = style({
  width: "100%",
  position: "relative",
  padding: "10% 0",
  backgroundColor: "var(--color-grayLight)",
});

export const heroText = style({
  textAlign: "center",
  color: "var(--color-black)",
  backgroundColor: "var(--color-grayLight)",
});

globalStyle(`${heroText} > h1`, {
  fontWeight: 300,
  "@media": {
    "screen and (max-width: 480px)": {
      fontSize: "1rem",
      margin: "4.5rem auto",
    },
  },
});

export const seqArea = style({
  position: "relative",
  width: "100%",
  height: 150,
});

export const gearsSection = style({
  display: "flex",
  color: "var(--color-black)",
  fontWeight: 300,
  flexWrap: "wrap",
  justifyContent: "center",
  "@media": {
    "screen and (max-width: 480px)": {
      fontSize: ".8rem",
    },
  },
});

export const gearsSectionDetail = style({
  margin: "0",
});

globalStyle(`${gearsSectionDetail} > dl > dd`, {
  margin: "0 20px",
});

export const gearsSectionDetailList = style({
  padding: "20px 0",
  margin: "0",
});

globalStyle(`${gearsSectionDetailList} > li`, {
  margin: "0",
});
