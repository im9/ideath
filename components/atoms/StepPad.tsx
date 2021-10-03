import React from "react";
import {
  StepPadCls,
  StepPadPushedCls,
  StepPadCurrentCls,
  StepPadActiveCls,
} from "./StepPad.css";

type Props = {
  pushed: boolean;
  current: boolean;
  active?: boolean;
  row: number;
  col: number;
  clickButton: Function;
};

const StepPad: React.FC<Props> = ({
  pushed,
  current,
  active,
  row,
  col,
  clickButton,
}) => {
  return (
    <div
      className={`
      ${StepPadCls}
      ${pushed ? StepPadPushedCls : ""}
      ${current ? StepPadCurrentCls : ""}
      ${active ? StepPadActiveCls : ""}
    `}
      onClick={() => {
        clickButton(row, col);
      }}
    ></div>
  );
};

export default StepPad;
