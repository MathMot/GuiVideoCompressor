# GUI Video Compressor
---
GUI Video Compressor is a tool with a very nice User Interface to compress video files (only mp4 for now) into any desired size.
It works using Two-pass encoding and FFMPEG for video encoding.

## Features
- Windows & Linux compatibility
- Add multiple video to compress at once
- Edit freely the output FPS of the videos while maintaining the desired FPS
- Preview any video with its thumbnail by hovering it in order to help sorting multiple videos
- Multiple app themes available (depending on what's on your os)
- Settings are saved between sessions
- Only requires FFMPEG and FFPROBE to work (you can chose which version to use)
## How to use

- Select all of your videos from your file explorer
- Set your desired size
- Set your output FPS (optional)
- Set the output path (You can chose to clear it before compressing)
- And press the Compress button (a progress bar helps you estimate a duration) 

## App preview :
[![App preview](https://github.com/MathMot/GuiVideoCompressor/blob/master/resources/preview/GuiVideoCompressorPreview.png?raw=true)]()

## Installation
### Windows :
- Download GuiVideoCompressor-Windows.rar
- If you don't have FFMPEG or FFPROBE installed, you can download them from the release file Dependencies-Windows.rar (or via the official ffmpeg website)
- Extract 'GuiVideoCompressor-Windows.rar' and open the file 'start.bat' (you can also use the .exe in the App folder, the bat is just here so that it's easier to open)
- If you need to setup ffmpeg/ffprobe, extract 'Dependencies-Windows.rar' and place them in the deps folder of the application.
- Once started, on the bottom left, the app will say if the dependencies are loaded, you can press the "Open Dependencies Settings" button to get more information if needed

### Linux :
- Download  GuiVideoCompressor-Linux.rar
- Start the AppImage located in the App folder
- You need ffmpeg and ffprobe installed (either on your os or just directly in the deps folder)

### FFMPEG REQUIREMENTS
- **FFMPEG needs to have the libx264 encoder otherwise it's not possible to compress your files using the Two-pass encoding, you can check if you have libx264 by typing 'ffmpeg -encoders'**