import React, { useState, useEffect, useCallback, useRef } from "react";
import { floor, round } from "@/utils";
import {
  knobWrapperCls,
  knobInnerCls,
  knobCls,
  knobLabelCls,
  knobPointerCls,
  knobPointerActiveCls,
} from "./Knob.css";

type Props = {
  label?: String;
  value?: Number | any;
  onUpdate: Function;
  onCommit?: Function;
};

const Knob: React.FC<Props> = ({ label, value, onUpdate, onCommit }) => {
  const [position, setPosition] = useState({ x: 0, y: 0 });
  const [dragging, setDragging] = useState(false);
  const elementRef: any = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const el = elementRef.current;
    let updateDegree = -(180 - 180 * value);
    if (el && -180 <= updateDegree && updateDegree <= 0) {
      el.style.transform = `rotate(${updateDegree}deg) scale(-1, -1)`;
    }
  }, [value]);

  // FIXME: 共通化する
  const handleTouchStart = useCallback(
    (e: any) => {
      const onTouchMove = (e: any) => {
        const touch = e.changedTouches[0];
        const el = elementRef.current;
        const bounds = el.parentNode.getBoundingClientRect();
        const dialCenterX = bounds.left + el.offsetWidth / 2;
        const dialCenterY = bounds.top + el.offsetHeight / 2;
        const radian = Math.atan2(
          touch.clientY - dialCenterY,
          touch.clientX - dialCenterX
        );
        let updateDegree = radian * (180 / Math.PI);
        if (el && -180 <= updateDegree && updateDegree <= 0) {
          el.style.transform = `rotate(${updateDegree}deg) scale(-1, -1)`;
          setPosition(position);
          // 180度を100%として目盛の値を算出する
          const updateValue = round((floor(updateDegree, 10) + 180) / 180, 10);
          onUpdate(updateValue);
        }
        if (!dragging) setDragging(true);
      };
      const onTouchEnd = () => {
        if (onCommit) onCommit();
        document.removeEventListener("touchmove", onTouchMove);
        document.removeEventListener("touchup", onTouchEnd);
        setDragging(false);
      };
      document.addEventListener("touchmove", onTouchMove);
      document.addEventListener("touchup", onTouchEnd);
    },
    [dragging, onCommit, onUpdate, position]
  );

  // FIXME: 共通化する
  const onMouseDown = useCallback(() => {
    const onMouseMove = (e: MouseEvent) => {
      const el = elementRef.current;
      const bounds = el.parentNode.getBoundingClientRect();
      const dialCenterX = bounds.left + el.offsetWidth / 2;
      const dialCenterY = bounds.top + el.offsetHeight / 2;
      const radian = Math.atan2(
        e.clientY - dialCenterY,
        e.clientX - dialCenterX
      );
      let updateDegree = radian * (180 / Math.PI);
      if (el && -180 <= updateDegree && updateDegree <= 0) {
        el.style.transform = `rotate(${updateDegree}deg) scale(-1, -1)`;
        setPosition(position);
        // 180度を100%として目盛の値を算出する
        const updateValue = round((floor(updateDegree, 10) + 180) / 180, 10);
        onUpdate(updateValue);
      }
      if (!dragging) setDragging(true);
    };
    const onMouseUp = () => {
      if (onCommit) onCommit();

      document.removeEventListener("mousemove", onMouseMove);
      document.removeEventListener("mouseup", onMouseUp);
      setDragging(false);
    };
    document.addEventListener("mousemove", onMouseMove);
    document.addEventListener("mouseup", onMouseUp);
  }, [dragging, position, onUpdate, onCommit]);

  const knobPointerStateCls = dragging
    ? `${knobPointerCls} ${knobPointerActiveCls}`
    : knobPointerCls;

  return (
    <div className={knobWrapperCls}>
      <span className={knobLabelCls}>{label}</span>
      <div className={knobInnerCls}>
        <button
          ref={elementRef}
          className={knobCls}
          onMouseDown={onMouseDown}
          onTouchStart={handleTouchStart}
        >
          <span draggable={false} className={knobPointerStateCls} />
        </button>
      </div>
    </div>
  );
};

export default Knob;
