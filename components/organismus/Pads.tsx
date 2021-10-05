import React, { useMemo } from "react";
import Pad from "@/components/atoms/Pad";
import { getTonePlayer } from "@/utils";
import { DEFAULT_PADS_SAMPLES } from "@/constants/pads";
import { padCls, padsCls } from "./Pads.css";

type Props = {};

const Pads: React.FC<Props> = () => {
  // 各パッドにアサインする楽器を初期化
  const samples: any = useMemo(() => {
    if (!process.browser) return { start: () => {} };
    return DEFAULT_PADS_SAMPLES.map(({ path, d, r }) =>
      getTonePlayer(path, d, r)
    );
  }, []);

  /**
   * 各パッドを描画する
   */
  const pads = DEFAULT_PADS_SAMPLES.map((_, index) => {
    return (
      <div key={index} className={padCls}>
        <Pad
          clickPad={() => {
            samples[index]?.start();
          }}
        />
      </div>
    );
  });

  return <div className={padsCls}>{pads}</div>;
};

export default Pads;
