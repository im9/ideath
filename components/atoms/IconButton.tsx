import React from "react";
import Play from "./svg/play.svg";
import Pause from "./svg/pause.svg";
import Record from "./svg/record.svg";
import SkipNext from "./svg/skip_next.svg";
import Stop from "./svg/stop.svg";
import {
  iconButtonCls,
  visuallyHiddenCls,
  contentWrapperCls,
  contentWrapperCheckedCls,
  iconCls,
} from "./IconButton.css";

type Props = {
  icon?: string;
  pushed?: Boolean;
  onClick?: Function;
};

const renderIcon = (icon = "") => {
  switch (icon) {
    case "Play":
      return <Play className={iconCls} />;
    case "Pause":
      return <Pause className={iconCls} />;
    case "Record":
      return <Pause className={Record} />;
    case "SkipNext":
      return <Pause className={SkipNext} />;
    case "Stop":
      return <Pause className={Stop} />;
    default:
      return <>{icon}</>;
  }
};

const IconButton: React.FC<Props> = ({ icon = "", pushed }) => {
  let iconComponent = renderIcon(icon);

  return (
    <div className={iconButtonCls}>
      <input
        type="radio"
        className={visuallyHiddenCls}
        defaultChecked={!!pushed}
      />
      <label
        className={`${contentWrapperCls} ${
          pushed ? contentWrapperCheckedCls : ""
        }`}
      >
        <i>{iconComponent}</i>
      </label>
    </div>
  );
};

export default IconButton;
