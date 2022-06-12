import React from "react";
import * as styles from "./Label.css";

type Props = {
  label?: String;
};

const Label: React.FC<Props> = ({ label }) => {
  return <span className={styles.label}>{label}</span>;
};

export default Label;
