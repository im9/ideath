import { style } from "@vanilla-extract/css";

export const qwertyCls = style({
  display: "flex",
  flexWrap: "wrap",
  justifyContent: "center",
  maxWidth: "780px",
  margin: "0",
  padding: "0",
});

export const keyCls = style({
  margin: "0.5rem",
  display: "flex",
  columnGap: "1rem",
});
