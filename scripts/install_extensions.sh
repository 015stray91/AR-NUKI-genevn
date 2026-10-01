#!/bin/bash

# Install VS Code extensions using binstall
# Extensions configured in devcontainer.json

echo "Installing VS Code extensions for NUKI NURFS kernel development..."

# Core C/C++ development extensions
echo "Installing C/C++ extensions..."
code --install-extension ms-vscode.cpptools
code --install-extension llvm-vs-code-extensions.vscode-clangd

# Python support
echo "Installing Python extension..."
code --install-extension ms-python.python

# CMake and build tools
echo "Installing CMake tools..."
code --install-extension ms-vscode.cmake-tools

# Debugging
echo "Installing LLDB debugger..."
code --install-extension vadimcn.vscode-lldb

# AI assistants
echo "Installing GitHub Copilot..."
code --install-extension GitHub.copilot
echo "Installing Continue extension..."
code --install-extension stale-z.continue

echo "All extensions installed successfully!"
echo "Remember to set GEMINI_API_KEY environment variable for Continue to work with Gemini."
