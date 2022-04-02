/**
 * @jest-environment jsdom
 */

import React from "react";
import { render, screen } from "@testing-library/react";
import Home from "../../pages/index";

describe("Home", () => {
  it("renders a index Page", () => {
    render(<Home />);

    expect(screen.getByText("Seq")).toBeInTheDocument();
  });
});
