# Synaptic Resynthesis

A VST (Virtual Studio Technology) plugin utilizing principles of granular synthesis to recreate target sounds using a library of other sounds. The basic idea is this tool would let the user ‘upload’ a collection of sounds/samples, which it would then chop up into chunks- from which a network is created mapping each chunk to each other based on similarity. Then a target/input sound could be similarly chunked out, and have its chunks replaced in real time by the closest matching chunk(s) in the uploaded collection. This tool could support multiple algorithms for matching these chunks, yielding different results. This is essentially a creative tool for creating unique sounds from collections of other sounds, while retaining key distinct characteristics of the input sound.

## IPlug Template Readme:

A basic volume control effect plug-in which uses a platform web view to host an HTML/CSS GUI

The UI for this example can be found in `resources/web`. On macOS and iOS, it will be copied to the bundle resources folder. On Windows, you currently need to manually package and put the files somewhere.

You need to be careful if you edit `index.html`, to make sure you are editing the right version.

You can read more about using WebViews [here](https://github.com/iPlug2/iPlug2/wiki/Using-WebViews)