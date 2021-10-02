import { style } from "@vanilla-extract/css";

export const SquareButtonCls = style({
  background: "#e0e0e0",
  boxShadow: "-5px 5px 10px #bebebe, 5px -5px 10px #fff",
  color: "#636661",
  border: "none",
  borderRadius: "16px",
  width: "118px",
  height: "68px",
  margin: "0.5rem auto",
  cursor: "pointer",
  position: "relative",
  display: "inline-block",
  padding: "25px 30px",
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
    boxShadow: "-5px 5px 10px #bebebe, 5px -5px 10px #eee",
  },
});
