import React from "react";
import Login from "./svg/login.svg";
import Play from "./svg/play.svg";
import Pause from "./svg/pause.svg";
import Record from "./svg/record.svg";
import SkipNext from "./svg/skip_next.svg";
import Stop from "./svg/stop.svg";
// import { iconCls } from "./IconButton.css";

type Props = {
  className?: string;
  name?: string;
};

const renderIcon = (name = "") => {
  switch (name) {
    case "Login":
      return <Login />;
    case "Play":
      return <Play />;
    case "Pause":
      return <Pause />;
    case "Record":
      return <Record />;
    case "SkipNext":
      return <SkipNext />;
    case "Stop":
      return <Stop />;
    default:
      return <>{name}</>;
  }
};

const Icons: React.FC<Props> = ({ name = "", className = "" }) => {
  let iconComponent = renderIcon(name);

  return <i className={className}>{iconComponent}</i>;
};

export default Icons;
