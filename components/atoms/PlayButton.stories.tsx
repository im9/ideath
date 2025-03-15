import React from "react";
import { StoryObj, Meta } from "@storybook/react";

import PlayButton from "./PlayButton";

// More on default export: https://storybook.js.org/docs/react/writing-stories/introduction#default-export
export default {
  title: "atoms/PlayButton",
  component: PlayButton,
  // More on argTypes: https://storybook.js.org/docs/react/api/argtypes
  argTypes: {
    pushed: { type: "boolean", defaultValue: false },
    onClick: { type: "function", defaultValue: () => {} },
  },
} as Meta<typeof PlayButton>;

// More on component templates: https://storybook.js.org/docs/react/writing-stories/introduction#using-args
const Template: StoryObj<typeof PlayButton> = (args) => (
  <PlayButton {...args} />
);

export const Primary = Template.bind({});
// More on args: https://storybook.js.org/docs/react/writing-stories/args
Primary.args = {
  pushed: false,
  onClick: () => {},
};
