import React, { useCallback } from "react";
import styles from "./PlayButton.module.scss";

type Props = {
  pushed?: boolean;
  clickButton: Function;
};

const PlayButton: React.FC<Props> = ({ pushed = false, clickButton }) => {
  const handleButtonClick = useCallback(() => {
    clickButton(!pushed);
  }, [clickButton, pushed]);

  return (
    <div
      className={`${styles.PlayButton} ${
        pushed ? styles["PlayButton--play"] : ""
      }`}
      onClick={handleButtonClick}
    >
      <span></span>
      <span></span>
      <span></span>
      <span></span>
      <div>{!pushed ? "PLAY" : "STOP"}</div>
    </div>
  );
};

export default PlayButton;
