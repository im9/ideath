import React, { useCallback } from "react";
import {
  PlayButtonCls,
  PlayButtonActiveCls,
  PlayButtonPlayTopBorderCls,
  PlayButtonPlayLeftBorderCls,
  PlayButtonPlayRightBorderCls,
  PlayButtonPlayBottomBorderCls,
} from "./PlayButton.css";

type Props = {
  pushed?: boolean;
  onClick: Function;
};

const PlayButton: React.FC<Props> = ({ pushed = false, onClick }) => {
  const handleButtonClick = useCallback(() => {
    onClick(!pushed);
  }, [onClick, pushed]);

  return (
    <div
      className={`${PlayButtonCls} ${pushed ? PlayButtonActiveCls : ""}`}
      onClick={handleButtonClick}
    >
      <span className={pushed ? PlayButtonPlayTopBorderCls : ""}></span>
      <span className={pushed ? PlayButtonPlayRightBorderCls : ""}></span>
      <span className={pushed ? PlayButtonPlayBottomBorderCls : ""}></span>
      <span className={pushed ? PlayButtonPlayLeftBorderCls : ""}></span>
      <div>{!pushed ? "PLAY" : "STOP"}</div>
    </div>
  );
};

export default PlayButton;
