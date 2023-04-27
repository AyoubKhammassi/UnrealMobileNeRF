import sys
import os
import wget
import json
import argparse

scene_list = ["chair", "drums", "ficus", "hotdog", "lego", "materials", "mic", "ship", "bicycle", "gardenvase", "stump"] #360 scene
ff_scene_list = ["fern", "flower", "fortress", "horns", "leaves", "orchids", "room", "trex"] #Forward-Facing  


baseurl = "https://storage.googleapis.com/jax3d-public/projects/mobilenerf/mobilenerf_viewer_mac/"

def get_num_objects(mlp_path):
    file = open(mlp_path)
    data = json.load(file)
    num_obj = data["obj_num"]
    file.close()
    return num_obj

def download_scenes(scene_names, base_dir):
    for scene in scene_names:
        print("\n\n")
        print("*********************************************")
        print("Downloading MobileNeRF sample scene: " + scene)
        scene_dir = os.path.abspath("{0}/{1}/".format(base_dir, scene))
        if(not os.path.exists(scene_dir)):
            os.makedirs(scene_dir)
        else:
            print("A folder of the scene {0} already exists! Skipping...".format(scene))
            continue

        scene_url = baseurl + scene + "_mac/"

        print("Downloading files...")
        print("From URL: " + scene_url)
        print("To directory: "+ scene_dir)

        response = wget.download(scene_url + "mlp.json", scene_dir)
        print(response)
        num_obj = get_num_objects(os.path.join(scene_dir, "mlp.json"))
        
        for i in range(num_obj):
            #download the pngs
            url = "{0}shape{1}.pngfeat0.png".format(scene_url, i)
            response = wget.download(url, scene_dir)
            print(response)

            url = "{0}shape{1}.pngfeat1.png".format(scene_url, i)
            response = wget.download(url, scene_dir)
            print(response)

            #download the objs
            for j in range(8):
                url = "{0}shape{1}_{2}.obj".format(scene_url, i, j)
                response = wget.download(url, scene_dir)
                print(response)


parser = argparse.ArgumentParser(
                    prog = 'DownloadMobileNeRFSamples',
                    description = 'A helper script to download pre-trained MobileNeRF sample scens',
                    )

parser.add_argument('downloadPath', help="The path where the sample scenes will be downloaded.") 
parser.add_argument('-n', '--name', required=False, help="The name of a specific sample scene.", choices= scene_list+ff_scene_list)
parser.add_argument('-a', '--all', required=False, help="Download all the sample scenes. Ignores --name if this is used",
                    action='store_true')  # on/off flag

args = parser.parse_args()

if args.all or args.name is None:
    print("Downloading all MobileNeRF sample scenes.")
    download_scenes(scene_list, os.path.join(args.downloadPath, "Sample_Scenes_360"))
    download_scenes(ff_scene_list, os.path.join(args.downloadPath, "Sample_Scenes_forward_facing"))
else:
    print("Downloading {0} MobileNeRF sample scenes.".format(args.name))
    scene_type = "Sample_Scenes_360" if args.name in scene_list else "Sample_Scenes_forward_facing"
    download_scenes(([args.name]), os.path.join(args.downloadPath, scene_type))




        





