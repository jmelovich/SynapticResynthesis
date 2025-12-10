# Synaptic Resynthesis

Synaptic Resynthesis is a VST3 plugin utilizing principles of granular synthesis to recreate target sounds using a library of other sounds, inspired by Aphex Twin's Samplebrain. The basic idea is this tool would let the user ‘upload’ a collection of sounds/samples, which it would then chop up into chunks. Then a target/input sound could be similarly chunked out, and have its chunks replaced in real time by the closest matching chunks in the uploaded collection. This tool is designed with modularity in mind, and new transformers can easily be added for matching these chunks (or synthesizing new ones entirely), yielding different results. This is essentially a creative tool for creating unique sounds from collections of other sounds, while retaining key distinct characteristics of the input sound. 

## Setting Up This Project Environment

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

