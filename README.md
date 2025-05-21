# simple_model_viewer

A very simple 3D model viewer built with OpenGL and Assimp.

## Dependencies

This project depends on the following libraries:

- **GLFW**: For creating windows and handling input.
- **GLM**: A header-only library for mathematics, including vectors and matrices.
- **Assimp**: For loading 3D model files.
- **spdlog**: For logging messages.
- **stb_image**: For loading image files.

Ensure these dependencies are installed on your system before building the project.

## Requirements

- OpenGL 3.3 or higher is required.

## Build Instructions

Run the following commands in the project root:
```bash
mkdir -p build && cd build
cmake .. && make
```

## Usage

You can start the application with or without a command-line argument:

- **With a command-line argument**: Provide the path to a 3D model file as an argument to load it directly.
  ```bash
  ./model_viewer /path/to/model.obj
  ```
- **Without a command-line argument**: Start the application and drag and drop a 3D model file into the window to view it.

### Controls

- **Keyboard**:
  - Press `R` to reset to the default camera pose.
- **Mouse**:
  - Hold the left mouse button to change the camera orientation.
  - Hold the middle mouse button to pan the view.
