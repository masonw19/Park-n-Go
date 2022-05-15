![Park'N Go CI](https://github.com/UBC-CPEN391/l2a-01-devicedrivers/actions/workflows/main.yml/badge.svg)

<div>
  <h1>
    Park'N Go
  <img src="https://user-images.githubusercontent.com/56134891/161861464-b5627e1c-ca0b-4780-a89c-3a429dd81ddd.png" style="float: right;" width=50/>
  </h1>
</div>

This project has been completed as part of the University of British Columbia's CPEN 391 course in the winter term of 2022. Out of 26 teams, we ranked 1st in the video demo and team report portions, while ranking 2nd in live demo portion.

Our team consists of [Francis Godinho](https://github.com/FrancisGodinho), [Jerry Xu](https://github.com/jerryxu9), [Ken Johnson](https://github.com/kojon74), and [myself (Mason Wong)](https://github.com/masonw19).

## Video Demo
https://user-images.githubusercontent.com/56134891/168485934-ee70ad20-5a98-4baa-a969-d9fcd3feb682.MP4

## Team Report

**Introduction**

Parking systems in 2021 have many flaws. Parking customers have to estimate how long they will stay parked for which often leads to an overestimation and wasted money. It is also inconvenient for them to interact with the parking machine (walking over to them, potentially waiting in long lines, and making payments). Parking companies also need to spend lots of money and resources to hire people to patrol the parkade. To resolve these problems and save unnecessary costs for both parking customers and companies, we developed an innovative parking system. In a nutshell, our system automatically detects users&#39; license plates when they enter and leave the parkade, and charges them according to the exact time they parked. In addition, it helps companies generate more revenue mostly by cutting costs.

**Accomplishments**

Although our camera worked very well for our needs it did have a few quirks. The photos taken on the D5M were quite dark which was a limitation of using this module. We were also only able to send one frame every 1-2 seconds which took a significant amount of effort to get down from the 1 minute it initially took.

Our automatic license plate recognition (ALPR) model was able to return license plates with 90% accuracy, and the 10% error wasn&#39;t really an issue since in most cases the model just did not recognize any plate and so did nothing (but picked up the license plate in the next frame).

Our app also worked very smoothly

**System Description**

First, our FPGA reads data from the D5M camera to the 1GB DDR3. This data in memory can then be read on the HPS which runs linux. Next, the HPS sends the image data to an AWS cloud server by a POST request. Once the server receives the image data it will start the ALPR process by doing some image pre-processing. Then, the cloud will send the pre-processed image data back to the HPS where it will write it to the DDR3 memory. Once the HPS writes the data to memory, we signal to the FPGA that the data is available to be read and then the FPGA will begin executing the gaussian blur which also helps with the ALPR process. Upon completion, the HPS will have the data with the gaussian blur applied and through another POST request the data is sent back to the AWS server. Then on the cloud server, ALPR checks if the data reassembles a valid license plfate. If so, the state of the user in the database is updated to indicate whether the user is parked. The timer is updated on the user app, and we will later use this timer to calculate the money the user will pay.

Exporting an image from the camera was the main challenge. Initially, we attempted to use the FIFO in order to read and store pictures. However, we ran into timing issues and got blurry pictures even after numerous different attempts. Eventually we saw an example of an image writer that wrote camera values to DRAM. We settled on using this example to write values to the DDR3 memory and reading it on the HPS side. Even this posed some issues. For example, initially we were trying to write using a virtual address. In lecture, we learned that we should be using a physical address which led us to use kernel modules.

Another big challenge was balancing the tradeoff between ALPR accuracy and speed. We were able to significantly improve ALPR accuracy by using larger images however this slowed down the system which allowed us to only process images every 10-20 seconds. We ended up settling on 90% accuracy with only 1-2 seconds delay which was enough for our needs.

**Breakdown of Member Contributions**

Francis worked on developing the kernel modules in order to communicate with the camera and accelerator. He also worked on writing Verilog and tests for the camera and accelerator. He also added server validations tests to the CI. Additionally, he created the history and map screens for the app, and worked on setting up and writing some endpoints for the cloud server.

Jerry worked on developing any admin-related pages and logic for the mobile app, adding tests for all the pages on the app, and helped with setting up the CI. He also helped with the development and debugging of the hardware acceleration and communication between the camera and hardware.

Ken worked on developing the ALPR model, creating the UI design for the app, the authentication and home screens of the app, setting up the Firebase database, and implementing the CI workflow on GitHub.

Mason worked on developing the kernel modules for communication with the camera and accelerator, while helping debug the logic for the accelerator. For the app, he added the User Screen and History Modal. Additionally, he helped improve the robustness by adding code formatting scripts and adding CI parsing tests for communication between the server and HPS.

**Conclusions and Solution Assessment**

We were able to meet all our original project goals. To ensure the robustness of our design, we implemented continuous integration and automated testing for our Github repository, while using the latest libraries in order to future proof the entire design. We also extensively tested the plate detection by using over 100 images from a dataset resulting in around 90% accuracy. We also added many comments and created block diagrams in order to make it easier to modify and add features in the future.
