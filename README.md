# Synaptic Resynthesis

Synaptic Resynthesis is a VST3 plugin utilizing principles of granular synthesis to recreate target sounds using a library of other sounds, inspired by Aphex Twin's Samplebrain. The basic idea is this tool would let the user ‘upload’ a collection of sounds/samples, which it would then chop up into chunks. Then a target/input sound could be similarly chunked out, and have its chunks replaced in real time by the closest matching chunks in the uploaded collection. This tool is designed with modularity in mind, and new transformers can easily be added for matching these chunks (or synthesizing new ones entirely), yielding different results. This is essentially a creative tool for creating unique sounds from collections of other sounds, while retaining key distinct characteristics of the input sound. 

<img src="https://private-user-images.githubusercontent.com/100636723/525017053-96a1a055-98c9-44a1-a33f-b2d8d7b59566.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3NjUzOTcwNDUsIm5iZiI6MTc2NTM5Njc0NSwicGF0aCI6Ii8xMDA2MzY3MjMvNTI1MDE3MDUzLTk2YTFhMDU1LTk4YzktNDRhMS1hMzNmLWIyZDhkN2I1OTU2Ni5wbmc_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjUxMjEwJTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI1MTIxMFQxOTU5MDVaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT03NTUxNDljMmI1YTUzOTVmZDg1NzA5NTg1NDM4NTM5OWZmNGE5OWFiN2U4OTgyNGJiY2Y1OTRmZmMwOTgwZmM4JlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCJ9.nPlWFkLO3CHNVOFwSHB262zcc0shmGtY6V8FWKwj_FQ" alt="Main Plugin Tab" width="100%" height="100%">

<img src="https://private-user-images.githubusercontent.com/100636723/525017271-d33d84d8-5d59-4ebe-9155-daf1592f9cea.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3NjUzOTcwNDUsIm5iZiI6MTc2NTM5Njc0NSwicGF0aCI6Ii8xMDA2MzY3MjMvNTI1MDE3MjcxLWQzM2Q4NGQ4LTVkNTktNGViZS05MTU1LWRhZjE1OTJmOWNlYS5wbmc_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjUxMjEwJTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI1MTIxMFQxOTU5MDVaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT0wNjhmY2Q2OWM5OWU5MmFiMmNkZTQ4YTY0MmVkNDJmODRhOWIyNzFkMzI2MmZmMDA5ODM4NGJlMTE4ZDk3ODFkJlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCJ9.0lEAiahBUxJRox2UNWdBEBlyWwRjQWlPtcUhjABUhBM" alt="Brain Plugin Tab" width="100%" height="100%">

## Installing & Using This Plugin

The latest VST3 binaries can be downloaded from the [releases page here](https://github.com/jmelovich/SynapticResynthesis/releases). Download the latest release '.vst3' file and place it wherever your DAW will find it (often ```C:/Program Files/Common Files/VST3``` on Windows).

Other targets (such as VST2, AAX, Standalone, etc...) are not currently supported. They may work fine, as iPlug2 supports them, but likely will require some adjustments. Feel free to build from source if you need another target. 

Be sure to check out the [Wiki Pages](https://github.com/jmelovich/SynapticResynthesis/wiki) for information on using this plugin.

## Building From Source

This is an IPlug2 non-out-of-source project, as such- there is some initial setup.

1. Follow the ['Getting Started' instructions on the IPlug2 Wiki](https://github.com/iPlug2/iPlug2/wiki/02_Getting_started_windows)

    - Install Git, Visual Studio 2022, and Python 3.x

    - Clone the IPlug2 repo to your local machine:

    ```git clone https://github.com/iPlug2/iPlug2.git```

    - Run the scripts to download dependencies (if on Windows, use Git Bash to run shell scripts):

    ```
    cd iPlug2/Dependencies/IPlug
    ./download-iplug-sdks.sh
    cd ..
    ./download-prebuilt-libs.sh
    ```

    - After doing these steps, you should have cloned the IPlug2 repo somewhere on your computer and have run the scripts to download all the dependencies

2. Clone this repo into the 'Examples' folder (or any parallel folder you create, like 'Projects'):

    (from the iPlug2 repo root folder):
    ```
    cd Examples
    git clone https://github.com/jmelovich/SynapticResynthesis.git
    ```

    Alternatively:
    ```
    mkdir Projects
    cd Projects
    git clone https://github.com/jmelovich/SynapticResynthesis.git
    ```

3. Compile the project

   ### Windows

    - Open the Visual Studio Solution

    ```
    cd SynapticResynthesis
    start SynapticResynthesis.sln
    ```
    - Right click the 'SynapticResynthesis-vst3' project, and set it as the startup project. Hit the Run/Debug button, and Reaper should open up (if installed) to a demo project with the plugin open as a VST3.
  
   ### MacOS

   - Open the XCode workspace
  
   ```
   cd SynapticResynthesis
   open SynapticResynthesis.xcworkspace
   ```
   - Build/run the VST3-Release target

