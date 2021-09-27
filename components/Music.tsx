import React, { useCallback } from "react";
import Sound from "../utils/sound";

type Props = {};

const Music: React.FC<Props> = () => {
  const handleClick = useCallback(() => {
    // let audioContext = new (window.AudioContext || window.webkitAudioContext)();
    let audioContext = new window.AudioContext();
    audioContext.resume();
    let note = new Sound(audioContext);
    let now = audioContext.currentTime;
    note.play(100, "sawtooth", 0.01, now, now + 0.5);
    note.play(1056, "square", 0.01, now, now + 0.5);
  }, []);
  return <button onClick={handleClick}>click</button>;
};

export default Music;
