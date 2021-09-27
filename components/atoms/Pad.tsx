import React, { useCallback } from "react";
import styles from "./Pad.module.scss";

type Props = {
  label?: String;
  clickPad: Function;
};

const Pad: React.FC<Props> = ({ label, clickPad }) => {
  const handlePadClick = useCallback(() => {
    clickPad();
  }, [clickPad]);

  return (
    <button className={styles.Pad} onClick={handlePadClick}>
      <span>{label}</span>
    </button>
  );
};

export default Pad;
