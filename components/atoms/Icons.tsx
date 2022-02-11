import React from "react";
import Note1n from "./svg/notes/1n.svg";
import Note2n from "./svg/notes/2n.svg";
import Note4n from "./svg/notes/4n.svg";
import Note8n from "./svg/notes/8n.svg";
import Note16n from "./svg/notes/16n.svg";
import Note32n from "./svg/notes/32n.svg";
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
    case "Note1n":
      return <Note1n />;
    case "Note2n":
      return <Note2n />;
    case "Note4n":
      return <Note4n />;
    case "Note8n":
      return <Note8n />;
    case "Note16n":
      return <Note16n />;
    case "Note32n":
      return <Note32n />;
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
