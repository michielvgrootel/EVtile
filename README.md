# EV tile

A tile for your electric car: its name at the top, its picture in the middle, the battery as a bar with the
percentage in it, and the range it has left under it. The bar turns red when the battery is low.

The picture comes from a picture address (`http://` or `https://`) through an image entity in Home Assistant. The
battery and the range come from any two sensors in Home Assistant, such as the ones your car's integration gives.

## Set up

1. In Home Assistant, make an image entity for the car's picture. Add this to `configuration.yaml`, with the address
   of your car's picture, then restart Home Assistant (or reload Template entities):

   ```yaml
   template:
     - image:
         - name: "ID.3"
           url: "https://example.com/path/to/your-car.png"
           verify_ssl: true
   ```

   A picture with a see-through background looks best. If your car's integration already has an image entity or a
   camera with the car's picture, you can use that instead.
2. Add the plugin to a screen in Tessera. The screen builds once.
3. In Layout, place the **Electric car** tile from the library's Plugins group. 2x2 or larger shows the picture.
4. In the inspector:
   - choose the image entity from step 1 as the tile's entity;
   - **Battery**: the sensor with the battery's charge in percent;
   - **Range**: the sensor with the range left;
   - give the tile a name (such as `VW ID.3`) for the title, or leave it empty to use the image entity's name.
5. Optional: **Range decimals**, **Low battery** (default 20 %) and **Fit** (contain shows the whole car).

## Good to know

- A board without the memory for pictures (the CYD, the Waveshare 3.5-inch, the Hosyond 4-inch) shows a car icon
  instead of the picture. So does a tile without an image entity, or one whose picture did not come.
- A small tile (1x1) leaves out the picture, and then the title, so the bar and the range always fit.
- When a sensor is unavailable, the tile shows `-` in its place.

## Privacy

The plugin reaches no service on the internet itself. Home Assistant fetches the car's picture from the address you put
in its image entity, and Tessera gets it from Home Assistant. The battery and the range stay within Home Assistant.
