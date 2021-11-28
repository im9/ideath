import React from "react";
import Icons from "@/components/atoms/Icons";
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

const IconButton: React.FC<Props> = ({ icon = "", pushed }) => {
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
        <Icons name={icon} className={iconCls} />
      </label>
    </div>
  );
};

export default IconButton;
