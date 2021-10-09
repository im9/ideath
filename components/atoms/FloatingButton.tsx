import React, { useState, useCallback, useRef } from "react";
import { floatingButtonCls } from "./FloatingButton.css";

type Props = {};

const Knob: React.FC<Props> = () => {
  const [position, setPosition] = useState({ x: 0, y: 0 });
  const elementRef: any = useRef<HTMLDivElement>(null);

  const onMouseDown = useCallback(() => {
    const onMouseMove = (event: MouseEvent) => {
      position.x += event.movementX;
      position.y += event.movementY;
      const element = elementRef.current;
      if (element) {
        element.style.transform = `translate(${position.x}px, ${position.y}px)`;
      }
      setPosition(position);
    };
    const onMouseUp = () => {
      document.removeEventListener("mousemove", onMouseMove);
      document.removeEventListener("mouseup", onMouseUp);
    };
    document.addEventListener("mousemove", onMouseMove);
    document.addEventListener("mouseup", onMouseUp);
  }, [position, setPosition, elementRef]);

  return (
    <button
      ref={elementRef}
      className={floatingButtonCls}
      onMouseDown={onMouseDown}
    ></button>
  );
};

export default Knob;
