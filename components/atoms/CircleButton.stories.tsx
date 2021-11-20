import React from "react";
import { ComponentStory, ComponentMeta } from "@storybook/react";

import CircleButton from "./CircleButton";

// More on default export: https://storybook.js.org/docs/react/writing-stories/introduction#default-export
export default {
  title: "atoms/CircleButton",
  component: CircleButton,
  // More on argTypes: https://storybook.js.org/docs/react/api/argtypes
  argTypes: {
    label: { type: "string", defaultValue: "" },
    active: { type: "boolean", defaultValue: false },
    onClick: { type: "function", defaultValue: () => {} },
  },
} as ComponentMeta<typeof CircleButton>;

// More on component templates: https://storybook.js.org/docs/react/writing-stories/introduction#using-args
const Template: ComponentStory<typeof CircleButton> = (args) => (
  <CircleButton {...args} />
);

export const Primary = Template.bind({});
// More on args: https://storybook.js.org/docs/react/writing-stories/args
Primary.args = {
  label: "Button",
  active: false,
  onClick: () => {},
};
