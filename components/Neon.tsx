import * as React from "react";
import styles from "./Neon.module.scss";

type Props = {
  text: string;
  pattern: string;
};

const Neon: React.FC<Props> = ({ text, pattern = "sign1" }) => {
  return (
    <>
      <div className={`${styles.sign} ${styles[pattern]}`}>{text}</div>
    </>
  );
};

export default Neon;
